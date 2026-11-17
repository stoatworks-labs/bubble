#include "Film.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>
#include <thread>

namespace bubble::film
{
namespace
{
/// The CIE tables, generated from data/*.csv by CMake: kCmf rows (nm, x, y,
/// z) at 1 nm from 360 to 830 nm; kD65 rows (nm, S) at 1 nm from 300 to 830.
#include "CieTables.inc"

constexpr int kCmfRows = sizeof( kCmf ) / sizeof( kCmf[ 0 ] );
static_assert( kCmfRows == 471, "the CMFs run 360 .. 830 nm at 1 nm" );
constexpr int kD65First = 300;

double d65At( int nm )
{
	return kD65[ nm - kD65First ][ 1 ];
}

/// IEC 61966-2-1: XYZ to linear sRGB, rows.
constexpr double kXyzToRgb[ 3 ][ 3 ] = {
	{ 3.2406, -1.5372, -0.4986 },
	{ -0.9689, 1.8758, 0.0415 },
	{ 0.0557, -0.2040, 1.0570 },
};

/// The D65 white with Y = 1, and M times it.
struct White
{
	double xyz[ 3 ] = {};
	double rgb[ 3 ] = {};
	double norm     = 0.0;///< sum S_D65 ybar
};
const White& white()
{
	static const White w = [] {
		White out;
		for( int k = 0; k < kCmfRows; ++k )
			out.norm += d65At( static_cast< int >( kCmf[ k ][ 0 ] ) ) * kCmf[ k ][ 2 ];
		for( int k = 0; k < kCmfRows; ++k )
			for( int c = 0; c < 3; ++c )
				out.xyz[ c ] += d65At( static_cast< int >( kCmf[ k ][ 0 ] ) ) * kCmf[ k ][ 1 + c ] / out.norm;
		for( int r = 0; r < 3; ++r )
			for( int c = 0; c < 3; ++c )
				out.rgb[ r ] += kXyzToRgb[ r ][ c ] * out.xyz[ c ];
		return out;
	}();
	return w;
}

/// One polarisation's Airy reflectance from its interface reflectance R1 and
/// sin^2 of the phase (cos^2 for the no-half-wave model).
double airy( double R1, double s )
{
	const double F = 4.0 * R1 / ( ( 1.0 - R1 ) * ( 1.0 - R1 ) );
	return F * s / ( 1.0 + F * s );
}

struct Interface
{
	double n, cosT, R1s, R1p;
};
Interface interfaceAt( double theta, double lambdaNm, int model )
{
	Interface i;
	i.n             = model == kNoDispersion ? ReferenceIndex() : WaterIndex( lambdaNm );
	const double c1 = std::cos( theta ), st = std::sin( theta ) / i.n;
	i.cosT          = std::sqrt( 1.0 - st * st );
	const double rs = ( c1 - i.n * i.cosT ) / ( c1 + i.n * i.cosT );
	const double rp = ( i.n * c1 - i.cosT ) / ( i.n * c1 + i.cosT );
	i.R1s           = rs * rs;
	i.R1p           = rp * rp;
	return i;
}

void toRgb( const double xyz[ 3 ], double rgb[ 3 ] )
{
	const White& w = white();
	for( int r = 0; r < 3; ++r )
	{
		double v = 0.0;
		for( int c = 0; c < 3; ++c )
			v += kXyzToRgb[ r ][ c ] * xyz[ c ];
		rgb[ r ] = v / w.rgb[ r ];
	}
}

void buildTints( int model, TintTable& table )
{
	const auto start = std::chrono::steady_clock::now();
	table.rgba.assign( static_cast< size_t >( kTintColumns ) * kTintRows * 4, 0.0f );
	const White& w = white();
	for( int c = 0; c < 3; ++c )
		table.white[ c ] = w.rgb[ c ];
	std::atomic< int > next { 0 };
	auto work = [ & ]() {
		std::vector< double > acc( static_cast< size_t >( kTintColumns ) * 3 );
		for( int row = next++; row < kTintRows; row = next++ )
		{
			std::fill( acc.begin(), acc.end(), 0.0 );
			const double theta = row * kTintAngleStep * kPi / 180.0;
			const double stepH = kTintStepNm / CosRefracted( theta );//nm of h per column
			for( int k = 0; k < kCmfRows; ++k )
			{
				const double lambda = kCmf[ k ][ 0 ];
				const double s65    = d65At( static_cast< int >( lambda ) ) / w.norm;
				const double wx = s65 * kCmf[ k ][ 1 ], wy = s65 * kCmf[ k ][ 2 ], wz = s65 * kCmf[ k ][ 3 ];
				const Interface f = interfaceAt( theta, lambda, model );
				//delta_i = i dd; cos 2 delta by rotation, put back on its exact
				//value every 64 columns.
				const double dd = 2.0 * kPi * f.n * f.cosT * stepH / lambda;
				const double cr = std::cos( 2.0 * dd ), sr = std::sin( 2.0 * dd );
				double c2 = 1.0, s2 = 0.0;
				for( int i = 0; i < kTintColumns; ++i )
				{
					if( ( i & 63 ) == 0 )
					{
						c2 = std::cos( 2.0 * dd * i );
						s2 = std::sin( 2.0 * dd * i );
					}
					const double sin2 = 0.5 * ( 1.0 - c2 );
					const double s    = model == kNoHalfWave ? 1.0 - sin2 : sin2;
					const double Rs   = airy( f.R1s, s );
					const double R    = model == kSOnly ? Rs : 0.5 * ( Rs + airy( f.R1p, s ) );
					double* a         = &acc[ static_cast< size_t >( i ) * 3 ];
					a[ 0 ] += wx * R;
					a[ 1 ] += wy * R;
					a[ 2 ] += wz * R;
					const double nc = c2 * cr - s2 * sr;
					s2              = s2 * cr + c2 * sr;
					c2              = nc;
				}
			}
			for( int i = 0; i < kTintColumns; ++i )
			{
				double rgb[ 3 ];
				toRgb( &acc[ static_cast< size_t >( i ) * 3 ], rgb );
				float* o = &table.rgba[ ( static_cast< size_t >( row ) * kTintColumns + i ) * 4 ];
				o[ 0 ]   = static_cast< float >( rgb[ 0 ] );
				o[ 1 ]   = static_cast< float >( rgb[ 1 ] );
				o[ 2 ]   = static_cast< float >( rgb[ 2 ] );
				o[ 3 ]   = 1.0f;
			}
		}
	};
	const unsigned hw     = std::thread::hardware_concurrency();
	const unsigned helpers = std::clamp( hw > 1 ? hw - 1 : 0u, 0u, 7u );
	std::vector< std::thread > threads;
	for( unsigned t = 0; t < helpers; ++t )
		threads.emplace_back( work );
	work();
	for( std::thread& t : threads )
		t.join();
	table.seconds = std::chrono::duration< double >( std::chrono::steady_clock::now() - start ).count();
}
} // namespace

//===========================================================================
// Optics
//===========================================================================
double WaterIndex( double lambdaNm, double celsius, double density )
{
	//IAPWS R9-97, Table 1.
	static const double a[ 8 ] = { 0.244257733, 9.74634476e-3, -3.73234996e-3, 2.68678472e-4,
		                           1.58920570e-3, 2.45934259e-3, 0.900704920, -1.66626219e-2 };
	const double lambdaUV = 0.2292020, lambdaIR = 5.432937;
	const double T = ( celsius + 273.15 ) / 273.15, r = density / 1000.0, L = lambdaNm / 589.0, L2 = L * L;
	const double rhs = r
	                   * ( a[ 0 ] + a[ 1 ] * r + a[ 2 ] * T + a[ 3 ] * L2 * T + a[ 4 ] / L2 + a[ 5 ] / ( L2 - lambdaUV * lambdaUV )
	                       + a[ 6 ] / ( L2 - lambdaIR * lambdaIR ) + a[ 7 ] * r * r );
	return std::sqrt( ( 1.0 + 2.0 * rhs ) / ( 1.0 - rhs ) );
}

double ReferenceIndex()
{
	static const double n = WaterIndex( 550.0 );
	return n;
}

double CosRefracted( double theta )
{
	const double st = std::sin( theta ) / ReferenceIndex();
	return std::sqrt( 1.0 - st * st );
}

double Reflectance( double hNm, double theta, double lambdaNm, int model )
{
	const Interface f  = interfaceAt( theta, lambdaNm, model );
	const double delta = 2.0 * kPi * f.n * hNm * f.cosT / lambdaNm;
	const double sin2  = std::sin( delta ) * std::sin( delta );
	const double s     = model == kNoHalfWave ? 1.0 - sin2 : sin2;
	const double Rs    = airy( f.R1s, s );
	return model == kSOnly ? Rs : 0.5 * ( Rs + airy( f.R1p, s ) );
}

void TintDirect( double hNm, double theta, int model, double rgb[ 3 ] )
{
	const White& w   = white();
	double xyz[ 3 ]  = {};
	for( int k = 0; k < kCmfRows; ++k )
	{
		const double lambda = kCmf[ k ][ 0 ];
		const double R      = Reflectance( hNm, theta, lambda, model ) * d65At( static_cast< int >( lambda ) ) / w.norm;
		for( int c = 0; c < 3; ++c )
			xyz[ c ] += R * kCmf[ k ][ 1 + c ];
	}
	toRgb( xyz, rgb );
}

const TintTable& Tints( int model )
{
	static TintTable tables[ kOpticsModels ];
	static std::once_flag once[ kOpticsModels ];
	const int m = std::clamp( model, 0, kOpticsModels - 1 );
	std::call_once( once[ m ], [ m ] { buildTints( m, tables[ m ] ); } );
	return tables[ m ];
}

//===========================================================================
// Drainage
//===========================================================================
double MasterCurve( double u )
{
	if( !( u > 0.0 ) )
		return 1.0;
	return 1.0 - std::pow( -std::expm1( -std::pow( u / kFitA, kFitB ) ), kFitC );
}

double MasterInverse( double y )
{
	if( !( y < 1.0 ) )
		return 0.0;
	if( !( y > 0.0 ) )
		return std::numeric_limits< double >::infinity();
	//1 - (1 - y)^(1/c) without cancellation, then -ln of it.
	const double q = -std::expm1( std::log1p( -y ) / kFitC );
	return kFitA * std::pow( -std::log( q ), 1.0 / kFitB );
}

double ExponentN( int model )
{
	return model == kExponentOne ? 1.0 : kExponentN;
}

double TimeScale( double hMetres, int model )
{
	return kKappa * std::pow( hMetres, -ExponentN( model ) );
}

double BirthOffset( double hMidMetres, int model )
{
	return model == kNoBirthOffset ? 0.0 : -TimeScale( hMidMetres, model ) * MasterInverse( 0.5 );
}

double Amplitude( double tau, double t0, int model )
{
	const double span = tau - t0;
	if( !( span > 0.0 ) )
		return std::numeric_limits< double >::infinity();
	return std::pow( kKappa / span, 1.0 / ExponentN( model ) );
}

double Shape( double y, int model )
{
	return std::pow( MasterInverse( y ), 1.0 / ExponentN( model ) );
}

double FrontHeight( double tau, double t0, int model )
{
	return MasterCurve( ( tau - t0 ) / TimeScale( kCriticalNm * 1e-9, model ) );
}

double BurstTime( double t0, double reach, int model )
{
	if( !( reach < 1.0 ) )
		return std::numeric_limits< double >::infinity();
	return t0 + TimeScale( kCriticalNm * 1e-9, model ) * MasterInverse( 1.0 - std::max( reach, 0.0 ) );
}

double ShapeTableW( int i )
{
	return static_cast< double >( i ) / ( kShapeSize - 1 ) * std::pow( 0.5, 0.25 );
}

std::vector< float > ShapeTable( int model )
{
	std::vector< float > t( 2 * kShapeSize );
	for( int i = 0; i < kShapeSize; ++i )
	{
		const double w4 = std::pow( ShapeTableW( i ), 4.0 );
		t[ i ]               = static_cast< float >( std::min( Shape( w4, model ), kShapeCap ) );
		t[ kShapeSize + i ]  = static_cast< float >( std::min( Shape( 1.0 - w4, model ), kShapeCap ) );
	}
	return t;
}

//===========================================================================
// The burst
//===========================================================================
double RetractionSpeed( double hNm )
{
	return std::sqrt( 2.0 * kSurfaceTension / ( kDensity * hNm * 1e-9 ) );
}

double InverseSpeedPerRootNm()
{
	return std::sqrt( kDensity * 1e-9 / ( 2.0 * kSurfaceTension ) );
}

double BurstDuration( double aspect )
{
	return std::sqrt( 1.0 + aspect * aspect ) * kFilmHeight / RetractionSpeed( kMaxNm );
}

//===========================================================================
// The stirring
//===========================================================================
uint32_t Pcg( uint32_t v )
{
	const uint32_t state = v * 747796405u + 2891336453u;
	const uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

double Hash01( uint32_t generation, uint32_t index )
{
	return Pcg( generation * 2654435761u ^ Pcg( index + 0x9e3779b9u ) ) * ( 1.0 / 4294967296.0 );
}

Flow FlowFor( uint32_t generation, double aspect )
{
	Flow flow;
	const double W = kFilmHeight * aspect, H = kFilmHeight;
	for( int k = 0; k < kModes; ++k )
	{
		const uint32_t base = 10u * static_cast< uint32_t >( k );
		flow.p[ k ]         = 1 + std::min( 2, static_cast< int >( 3.0 * Hash01( generation, base ) ) );
		flow.q[ k ]         = 1 + std::min( 2, static_cast< int >( 3.0 * Hash01( generation, base + 1 ) ) );
		flow.weight[ k ]    = 0.5 + 0.5 * Hash01( generation, base + 2 );
		flow.frequency[ k ] = 0.05 + 0.25 * Hash01( generation, base + 3 );
		flow.phase[ k ]     = 2.0 * kPi * Hash01( generation, base + 4 );
		flow.bound += flow.weight[ k ] * kPi * std::hypot( flow.p[ k ] / W, flow.q[ k ] / H );
	}
	return flow;
}

void FlowCoefficients( const Flow& flow, double swirl, double aspect, double t, double c[ kModes ] )
{
	const double W = kFilmHeight * aspect, H = kFilmHeight;
	for( int k = 0; k < kModes; ++k )
	{
		//The phase reduced in double before the sine: t runs for hours.
		const double cycles = std::fmod( flow.frequency[ k ] * t, 1.0 );
		const double a      = swirl * flow.weight[ k ] / flow.bound * std::sin( 2.0 * kPi * cycles + flow.phase[ k ] );
		c[ k ]              = a * kPi / ( W * H );
	}
}

//===========================================================================
// The clock
//===========================================================================
void Clock::Restart()
{
	++generation;
	phase        = Phase::Draining;
	age          = 0;
	tau          = 0.0;
	burstMicros  = 0;
	emptyMicros  = 0;
	tauBase      = 0.0;
	ageBase      = 0;
	drainageBase = -1.0;
}

double Clock::tauAt( int64_t at ) const
{
	return tauBase + drainageBase * static_cast< double >( at - ageBase ) * 1e-6;
}

void Clock::burst( double front )
{
	phase           = Phase::Bursting;
	burstMicros     = 0;
	nucleation[ 0 ] = 0.15 + 0.7 * Hash01( generation, 100 );
	nucleation[ 1 ] = 1.0 - front < 0.04 ? 0.98 : 0.5 * ( 1.0 + front );
}

void Clock::Advance( int64_t micros, const Settings& s, bool pop, bool newFilm )
{
	if( newFilm )
		Restart();
	t0                = BirthOffset( s.thicknessUm * 1e-6, s.drainageModel );
	int64_t remaining = std::max< int64_t >( micros, 0 );
	for( int guard = 0; guard < 64; ++guard )
	{
		if( phase == Phase::Draining )
		{
			//Drainage scales the law's clock; a change rebases it so tau never
			//jumps, and at a constant rate tau is the same function of age.
			if( drainageBase < 0.0 )
			{
				ageBase      = age;
				drainageBase = s.drainage;
			}
			else if( s.drainage != drainageBase )
			{
				tauBase      = tauAt( age );
				ageBase      = age;
				drainageBase = s.drainage;
			}
			tau = tauAt( age );
			if( pop )
			{
				pop = false;
				burst( FrontHeight( tau, t0, s.drainageModel ) );
				continue;
			}
			const double tb = BurstTime( t0, s.reach, s.drainageModel );
			if( std::isfinite( tb ) )
			{
				const double need = static_cast< double >( ageBase ) + ( tb - tauBase ) / drainageBase * 1e6;
				if( need <= static_cast< double >( age + remaining ) )
				{
					const int64_t hit = std::max( age, static_cast< int64_t >( std::ceil( need ) ) );
					remaining -= hit - age;
					age = hit;
					tau = tauAt( age );
					burst( FrontHeight( tau, t0, s.drainageModel ) );
					continue;
				}
			}
			age += remaining;
			remaining = 0;
			tau       = tauAt( age );
			break;
		}
		if( phase == Phase::Bursting )
		{
			const int64_t span = static_cast< int64_t >( std::ceil( BurstDuration( s.aspect ) * s.slowMotion * 1e6 ) );
			if( burstMicros + remaining < span )
			{
				burstMicros += remaining;
				break;
			}
			remaining -= span - burstMicros;
			burstMicros = span;
			phase       = Phase::Empty;
			emptyMicros = 0;
			continue;
		}
		const int64_t span = std::llround( std::max( s.refillSeconds, 0.0 ) * 1e6 );
		if( emptyMicros + remaining < span )
		{
			emptyMicros += remaining;
			break;
		}
		remaining -= span - emptyMicros;
		Restart();
	}
}

} // namespace bubble::film
