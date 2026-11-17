// The checks, one flag each. Included into main.cpp's anonymous namespace.
//
// Every claim is measured out of the picture (the float readback of the
// host's framebuffer). What it is measured against is restated here from
// SPEC-bubble.md with the harness's own code: the CIE CSVs read by its own
// parser, IAPWS R9-97 evaluated by its own function, the film's reflectance
// from a complex 2x2 transfer matrix (Byrnes eqs. 6-15) rather than the Airy
// form, the master curve's inverse by bisection, the stirring traced with
// the C library's sine. Nothing here calls the plugin's tables or its
// closed forms to predict a picture; --model compares those tables with the
// harness's own numbers, offline.

//===========================================================================
// Error sources, each named once. Every tolerance below is a sum of these.
//===========================================================================
/// One float rounding.
constexpr double kFloat = 1.0 / 16777216.0;
/// The sRGB encode in the shader (GPU float pow) and the harness's decode in
/// double: 2^-17 relative (lithophane's measure, with a factor of two).
constexpr double kEncode = 1.0 / 131072.0;
/// A float under 2^-126 may be flushed to zero: absolute.
constexpr double kFlush = 1.1754943508222875e-38;

constexpr double kPi = 3.14159265358979323846;

/// One float's spacing at x.
double ulpOf( double x )
{
	const float f = static_cast< float >( std::fabs( x ) );
	return static_cast< double >( std::nextafter( f, std::numeric_limits< float >::infinity() ) - f );
}

/// The larger of two errors, NaN winning: std::max( worst, NaN ) keeps
/// worst, so a model that produced NaN would pass every "worst <= tolerance".
double worse( double a, double b )
{
	return ( a != a || b != b ) ? std::numeric_limits< double >::quiet_NaN() : std::max( a, b );
}
template< typename... T >
double worse( double a, double b, T... rest )
{
	return worse( worse( a, b ), rest... );
}

//===========================================================================
// The harness's own physics (SPEC-bubble.md, The mechanism).
//===========================================================================
namespace hp
{
using cd = std::complex< double >;

/// A CSV of numbers, with the C library's strtod; anything else is fatal.
std::vector< std::vector< double > > readCsv( const std::string& path )
{
	std::vector< std::vector< double > > rows;
	std::ifstream in( path );
	if( !in )
	{
		std::fprintf( stderr, "bbtest: cannot read %s\n", path.c_str() );
		std::exit( 2 );
	}
	std::string line;
	while( std::getline( in, line ) )
	{
		if( line.empty() )
			continue;
		std::vector< double > row;
		const char* p = line.c_str();
		while( *p )
		{
			char* end      = nullptr;
			const double v = std::strtod( p, &end );
			if( end == p )
			{
				std::fprintf( stderr, "bbtest: %s: not a number in '%s'\n", path.c_str(), line.c_str() );
				std::exit( 2 );
			}
			row.push_back( v );
			p = end;
			while( *p == ',' || *p == ' ' || *p == '\r' )
				++p;
		}
		rows.push_back( row );
	}
	return rows;
}

struct Cie
{
	std::vector< double > lambda, x, y, z, s;//s: D65 at lambda
	double norm = 0.0;                         //sum s ybar
	double white[ 3 ] = {};                    //M times the D65 white, Y = 1
};

/// IEC 61966-2-1, the harness's copy.
const double kM[ 3 ][ 3 ] = { { 3.2406, -1.5372, -0.4986 }, { -0.9689, 1.8758, 0.0415 }, { 0.0557, -0.2040, 1.0570 } };

const Cie& cie()
{
	static const Cie c = [] {
		Cie out;
		const auto cmf = readCsv( std::string( BUBBLE_DATA_DIR ) + "/CIE_xyz_1931_2deg.csv" );
		const auto d65 = readCsv( std::string( BUBBLE_DATA_DIR ) + "/CIE_std_illum_D65.csv" );
		std::map< int, double > illuminant;
		for( const auto& r : d65 )
			illuminant[ static_cast< int >( std::lround( r[ 0 ] ) ) ] = r[ 1 ];
		for( const auto& r : cmf )
		{
			out.lambda.push_back( r[ 0 ] );
			out.x.push_back( r[ 1 ] );
			out.y.push_back( r[ 2 ] );
			out.z.push_back( r[ 3 ] );
			out.s.push_back( illuminant.at( static_cast< int >( std::lround( r[ 0 ] ) ) ) );
		}
		double X = 0, Y = 0, Z = 0;
		for( size_t k = 0; k < out.lambda.size(); ++k )
		{
			out.norm += out.s[ k ] * out.y[ k ];
			X += out.s[ k ] * out.x[ k ];
			Y += out.s[ k ] * out.y[ k ];
			Z += out.s[ k ] * out.z[ k ];
		}
		const double w[ 3 ] = { X / out.norm, Y / out.norm, Z / out.norm };
		for( int r = 0; r < 3; ++r )
			out.white[ r ] = kM[ r ][ 0 ] * w[ 0 ] + kM[ r ][ 1 ] * w[ 1 ] + kM[ r ][ 2 ] * w[ 2 ];
		return out;
	}();
	return c;
}

/// IAPWS R9-97 eq. (1), solved for n. The coefficients restated from the
/// release's Table 1.
double iapws( double lambdaNm, double celsius, double rho )
{
	const double A0 = 0.244257733, A1 = 0.974634476e-2, A2 = -0.373234996e-2, A3 = 0.268678472e-3, A4 = 0.158920570e-2,
	             A5 = 0.245934259e-2, A6 = 0.900704920, A7 = -0.166626219e-1;
	const double uv = 0.2292020, ir = 5.432937;
	const double rb = rho / 1000.0, tb = ( celsius + 273.15 ) / 273.15, lb = lambdaNm / 589.0;
	const double q  = rb * ( A0 + A1 * rb + A2 * tb + A3 * lb * lb * tb + A4 / ( lb * lb ) + A5 / ( lb * lb - uv * uv ) + A6 / ( lb * lb - ir * ir ) + A7 * rb * rb );
	// q = (n^2 - 1) / (n^2 + 2)
	return std::sqrt( ( 2.0 * q + 1.0 ) / ( 1.0 - q ) );
}
double nWater( double lambdaNm )
{
	return iapws( lambdaNm, 20.0, 998.20715 );
}

/// Byrnes' transfer matrix for air | film (n, h nm) | air at incidence
/// theta: r = Mt10 / Mt00, R = |r|^2, for s (pol 0) or p (pol 1).
double tmm( double hNm, double theta, double lambdaNm, int pol, double n )
{
	const cd ns[ 3 ] = { 1.0, n, 1.0 };
	const double s0  = std::sin( theta );
	cd cosT[ 3 ];
	for( int j = 0; j < 3; ++j )
		cosT[ j ] = std::sqrt( cd( 1.0 ) - ( s0 / ns[ j ] ) * ( s0 / ns[ j ] ) );
	auto rt = [ & ]( int j, int k, cd& r, cd& t ) {
		if( pol == 0 )
		{
			const cd den = ns[ j ] * cosT[ j ] + ns[ k ] * cosT[ k ];
			r            = ( ns[ j ] * cosT[ j ] - ns[ k ] * cosT[ k ] ) / den;
			t            = 2.0 * ns[ j ] * cosT[ j ] / den;
		}
		else
		{
			const cd den = ns[ k ] * cosT[ j ] + ns[ j ] * cosT[ k ];
			r            = ( ns[ k ] * cosT[ j ] - ns[ j ] * cosT[ k ] ) / den;
			t            = 2.0 * ns[ j ] * cosT[ j ] / den;
		}
	};
	using M2 = std::array< cd, 4 >;// row-major 2x2
	auto mul = []( const M2& a, const M2& b ) {
		return M2 { a[ 0 ] * b[ 0 ] + a[ 1 ] * b[ 2 ], a[ 0 ] * b[ 1 ] + a[ 1 ] * b[ 3 ], a[ 2 ] * b[ 0 ] + a[ 3 ] * b[ 2 ], a[ 2 ] * b[ 1 ] + a[ 3 ] * b[ 3 ] };
	};
	cd r01, t01, r12, t12;
	rt( 0, 1, r01, t01 );
	rt( 1, 2, r12, t12 );
	const cd delta = 2.0 * kPi * ns[ 1 ] * hNm * cosT[ 1 ] / lambdaNm;
	const cd I( 0.0, 1.0 );
	M2 m = { 1.0 / t01, r01 / t01, r01 / t01, 1.0 / t01 };
	const M2 layer = { std::exp( -I * delta ) / t12, std::exp( -I * delta ) * r12 / t12, std::exp( I * delta ) * r12 / t12, std::exp( I * delta ) / t12 };
	m              = mul( m, layer );
	const cd r     = m[ 2 ] / m[ 0 ];
	return std::norm( r );
}

/// The film's tint, linear sRGB white-corrected (SPEC-bubble.md, Colour),
/// from the transfer matrix.
void tint( double hNm, double theta, double rgb[ 3 ] )
{
	const Cie& c    = cie();
	double xyz[ 3 ] = {};
	for( size_t k = 0; k < c.lambda.size(); ++k )
	{
		const double n = nWater( c.lambda[ k ] );
		const double R = 0.5 * ( tmm( hNm, theta, c.lambda[ k ], 0, n ) + tmm( hNm, theta, c.lambda[ k ], 1, n ) );
		const double w = c.s[ k ] * R / c.norm;
		xyz[ 0 ] += w * c.x[ k ];
		xyz[ 1 ] += w * c.y[ k ];
		xyz[ 2 ] += w * c.z[ k ];
	}
	for( int r = 0; r < 3; ++r )
		rgb[ r ] = ( kM[ r ][ 0 ] * xyz[ 0 ] + kM[ r ][ 1 ] * xyz[ 1 ] + kM[ r ][ 2 ] * xyz[ 2 ] ) / c.white[ r ];
}

//---------------------------------------------------------------------------
// Drainage, restated: f, its inverse by bisection, T, t0, A, G, the front.
//---------------------------------------------------------------------------
const double kA = 0.13145, kB = 0.48916, kC = 23.08939;
const double kN = 0.72, kKappa = 2.4e-4, kHcr = 30e-9;
/// f, written so that it keeps its precision where it is tiny (the film's
/// foot: y to 1e-27 at ten minutes), and its complement 1 - f where f is
/// near 1 (the top): the naive 1 - (1 - e)^c cancels in both.
double f( double u )
{
	return u <= 0.0 ? 1.0 : -std::expm1( kC * std::log1p( -std::exp( -std::pow( u / kA, kB ) ) ) );
}
double fComplement( double u )
{
	return u <= 0.0 ? 0.0 : std::pow( -std::expm1( -std::pow( u / kA, kB ) ), kC );
}
/// u with 1 - f( u ) = q, by bisection.
double finvComplement( double q )
{
	if( q <= 0.0 )
		return 0.0;
	double lo = 0.0, hi = 1.0;
	while( fComplement( hi ) < q )
		hi *= 2.0;
	for( int i = 0; i < 200; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		( fComplement( mid ) < q ? lo : hi ) = mid;
	}
	return 0.5 * ( lo + hi );
}
/// u with f( u ) = y, by bisection (above 0.5 through the complement).
double finv( double y )
{
	if( y >= 1.0 )
		return 0.0;
	if( y > 0.5 )
		return finvComplement( 1.0 - y );
	double lo = 0.0, hi = 1.0;
	while( f( hi ) > y )
		hi *= 2.0;
	for( int i = 0; i < 200; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		( f( mid ) > y ? lo : hi ) = mid;
	}
	return 0.5 * ( lo + hi );
}
double T( double hMetres )
{
	return kKappa * std::pow( hMetres, -kN );
}
double t0( double hMidMetres )
{
	return -T( hMidMetres ) * finv( 0.5 );
}
double A( double tau, double birth )
{
	return std::pow( kKappa / ( tau - birth ), 1.0 / kN );
}
double G( double y )
{
	return std::pow( finv( y ), 1.0 / kN );
}
/// G at y = 1 - q, from q.
double GComplement( double q )
{
	return std::pow( finvComplement( q ), 1.0 / kN );
}
double front( double tau, double birth )
{
	return f( ( tau - birth ) / T( kHcr ) );
}

//---------------------------------------------------------------------------
// The stirring, restated (SPEC-bubble.md, The stirring; Film.cpp's hash).
//---------------------------------------------------------------------------
struct Flow
{
	int p[ 6 ], q[ 6 ];
	double w[ 6 ], nu[ 6 ], phi[ 6 ], bound = 0.0;
};
Flow flowFor( uint32_t gen, double aspect )
{
	Flow fl;
	const double H = 0.025, W = H * aspect;
	for( int k = 0; k < 6; ++k )
	{
		const uint32_t b = 10u * static_cast< uint32_t >( k );
		fl.p[ k ]        = 1 + std::min( 2, static_cast< int >( 3.0 * hash01( gen, b ) ) );
		fl.q[ k ]        = 1 + std::min( 2, static_cast< int >( 3.0 * hash01( gen, b + 1 ) ) );
		fl.w[ k ]        = 0.5 + 0.5 * hash01( gen, b + 2 );
		fl.nu[ k ]       = 0.05 + 0.25 * hash01( gen, b + 3 );
		fl.phi[ k ]      = 2.0 * kPi * hash01( gen, b + 4 );
		fl.bound += fl.w[ k ] * kPi * std::sqrt( ( fl.p[ k ] / W ) * ( fl.p[ k ] / W ) + ( fl.q[ k ] / H ) * ( fl.q[ k ] / H ) );
	}
	return fl;
}
/// Normalised velocity (dX/dt, dY/dt) at film age t s, Swirl s m/s.
void velocity( const Flow& fl, double swirl, double aspect, double t, double X, double Y, double& vx, double& vy )
{
	const double H = 0.025, W = H * aspect;
	vx = vy = 0.0;
	for( int k = 0; k < 6; ++k )
	{
		const double a = swirl * fl.w[ k ] / fl.bound * std::sin( 2.0 * kPi * fl.nu[ k ] * t + fl.phi[ k ] );
		const double c = a * kPi / ( W * H );
		vx += c * fl.q[ k ] * std::sin( fl.p[ k ] * kPi * X ) * std::cos( fl.q[ k ] * kPi * Y );
		vy -= c * fl.p[ k ] * std::cos( fl.p[ k ] * kPi * X ) * std::sin( fl.q[ k ] * kPi * Y );
	}
}
/// Where the point (X, Y) at film age `seconds` came from at age 0: traced
/// back through the spec's discrete flow (the coefficients of each 1/720 s
/// step at its midpoint, midpoint RK2 within the step), in double.
void traceBack( const Flow& fl, double swirl, double aspect, int64_t steps, double& X, double& Y )
{
	const double dt = 1.0 / 720.0;
	for( int64_t n = steps; n >= 1; --n )
	{
		const double tm = ( static_cast< double >( n ) - 0.5 ) * dt;
		double vx, vy;
		velocity( fl, swirl, aspect, tm, X, Y, vx, vy );
		double mx = X - 0.5 * dt * vx, my = Y - 0.5 * dt * vy;
		velocity( fl, swirl, aspect, tm, mx, my, vx, vy );
		X = std::clamp( X - dt * vx, 0.0, 1.0 );
		Y = std::clamp( Y - dt * vy, 0.0, 1.0 );
	}
}

/// Taylor-Culick, restated: sqrt( 2 gamma / ( rho h ) ).
double speed( double hNm )
{
	return std::sqrt( 2.0 * 0.035 / ( 998.20715 * hNm * 1e-9 ) );
}
} // namespace hp

//===========================================================================
// The controls, restated from SPEC-bubble.md (Controls).
//===========================================================================
float pThickness( double um ) { return static_cast< float >( ( um - 0.2 ) / 3.8 ); }
double hThickness( float v ) { return 0.2 + 3.8 * std::clamp( static_cast< double >( v ), 0.0, 1.0 ); }
float pDrainage( double x ) { return static_cast< float >( std::log( x / 0.1 ) / std::log( 100.0 ) ); }
double hDrainage( float v ) { return 0.1 * std::pow( 100.0, std::clamp( static_cast< double >( v ), 0.0, 1.0 ) ); }
float pSwirl( double mm ) { return static_cast< float >( mm / 20.0 ); }
double hSwirl( float v ) { return 20.0 * std::clamp( static_cast< double >( v ), 0.0, 1.0 ); }
float pSlow( double x ) { return static_cast< float >( std::log( x ) / std::log( 1000.0 ) ); }
double hSlow( float v ) { return std::pow( 1000.0, std::clamp( static_cast< double >( v ), 0.0, 1.0 ) ); }
float pRefill( double s ) { return static_cast< float >( s / 10.0 ); }
double hRefill( float v ) { return 10.0 * std::clamp( static_cast< double >( v ), 0.0, 1.0 ); }
float pAngle( double deg ) { return static_cast< float >( deg / 60.0 ); }
double hAngle( float v ) { return 60.0 * std::clamp( static_cast< double >( v ), 0.0, 1.0 ); }
float pExposure( double e ) { return static_cast< float >( e / 50.0 ); }
double hExposure( float v ) { return 50.0 * std::clamp( static_cast< double >( v ), 0.0, 1.0 ); }

//===========================================================================
// Rigs and measurements.
//===========================================================================
/// A film that never bursts, does not swirl, at Exposure 6 (tints to 0.12
/// stay under the top code), facing it: the checks change what they test.
void defaults( Rig& rig )
{
	rig.Set( PT_THICKNESS, pThickness( 1.5 ) );
	rig.Set( PT_DRAINAGE, pDrainage( 4.0 ) );
	rig.Set( PT_SWIRL, 0.0f );
	rig.Set( PT_BLACK_FILM, 0 );
	rig.Set( PT_BURST, 1.0f );
	rig.Set( PT_SLOW_MOTION, pSlow( 100.0 ) );
	rig.Set( PT_REFILL, pRefill( 1.0 ) );
	rig.Set( PT_ANGLE, 0.0f );
	rig.Set( PT_EXPOSURE, pExposure( 6.0 ) );
	rig.Set( PT_MIX, 1.0f );
}

bool startRig( Rig& rig, int w, int h, const Perturb& perturb, const Floats* picture = nullptr, double fps = 60.0 )
{
	rig.fps = fps;
	if( !rig.Init( w, h, picture ) )
		return false;
	rig.plugin.HooksForTest() = perturb;
	defaults( rig );
	return true;
}

/// Press an event: up, then down, as a host's button does.
void press( Rig& rig, unsigned int id )
{
	rig.Set( id, 1.0f );
	rig.Set( id, 0.0f );
}

/// The output's linear light per channel at one pixel.
double linAt( const Floats& out, int w, int x, int y, int c )
{
	return linearOf( out[ ( static_cast< size_t >( y ) * w + x ) * 4 + c ] );
}

/// The look card with every pixel quantised to an exact 8-bit code.
Floats lookCard( int w, int h )
{
	Floats card = buildCard( w, h );
	for( size_t i = 0; i < card.size(); ++i )
		card[ i ] = static_cast< float >( std::lround( card[ i ] * 255.0f ) ) / 255.0f;
	return card;
}

/// The film's age after rendered frame index k (0-based) of a rig at fps:
/// the first frame has no interval; later ones telescope.
int64_t ageAt( int k, double fps, double offset = 0.0, double scale = 1.0, double unit = 1.0 )
{
	// The plugin's arithmetic: raw = offset + scale * ( f / fps ); micros =
	// llround( raw * unit * 1e6 ); the intervals telescope.
	const int64_t first = std::llround( ( offset + scale * ( 0.0 / fps ) ) * unit * 1e6 );
	return std::llround( ( offset + scale * ( static_cast< double >( k ) / fps ) ) * unit * 1e6 ) - first;
}

/// Run `fn( i )` for i in [0, n) on up to 8 threads (the machine is shared).
template< typename F >
void parallelFor( int n, F&& fn )
{
	const int threads = std::max( 1, std::min( 8, static_cast< int >( std::thread::hardware_concurrency() ) ) );
	std::atomic< int > next( 0 );
	std::vector< std::thread > pool;
	for( int t = 0; t < threads; ++t )
		pool.emplace_back( [ & ] {
			for( int i = next++; i < n; i = next++ )
				fn( i );
		} );
	for( std::thread& th : pool )
		th.join();
}

/// Bilinear interpolation's bound on a table read between nodes: 1/8 of the
/// worst sum of second differences along the two axes, x 1.25 for the
/// higher-order terms (|e| <= ( dx^2 |f_xx| + dy^2 |f_yy| ) / 8).
double curvatureBound( const film::TintTable& table )
{
	auto node = [ & ]( int i, int j, int c ) { return static_cast< double >( table.rgba[ ( static_cast< size_t >( j ) * film::kTintColumns + i ) * 4 + c ] ); };
	double curvature = 0.0;
	for( int j = 0; j < film::kTintRows; ++j )
		for( int i = 0; i < film::kTintColumns; ++i )
			for( int c = 0; c < 3; ++c )
			{
				double d2x = 0.0, d2y = 0.0;
				if( i > 0 && i + 1 < film::kTintColumns )
					d2x = std::fabs( node( i - 1, j, c ) - 2.0 * node( i, j, c ) + node( i + 1, j, c ) );
				if( j > 0 && j + 1 < film::kTintRows )
					d2y = std::fabs( node( i, j - 1, c ) - 2.0 * node( i, j, c ) + node( i, j + 1, c ) );
				curvature = worse( curvature, d2x + d2y );
			}
	return curvature / 8.0 * 1.25;
}
/// The plugin's table's bound: the tolerance every picture check allows for
/// reading the colour table between nodes (--model measures it holds).
double tintBound()
{
	static const double bound = curvatureBound( film::Tints( film::kOptics ) );
	return bound;
}

/// What the composite must show for a uniform film h nm at theta degrees
/// over a clip of linear light `clip` (alpha 1) at Exposure E, per channel,
/// and how far it may be off: the table's bound; the column's slope times
/// the float error in the column position (h cos theta_t / 2, ~8 roundings);
/// the row's step times TintRowT's rounding; the interpolation's own float
/// arithmetic; then the encode and decode (kEncode).
struct Expect
{
	double want[ 3 ];
	double tol[ 3 ];
};
Expect expectFilm( double hNm, double deg, double E, double clip )
{
	Expect e;
	const double theta = deg * kPi / 180.0;
	double rgb[ 3 ];
	hp::tint( hNm, theta, rgb );
	const film::TintTable& t = film::Tints( film::kOptics );
	auto node = [ & ]( int i, int j, int c ) { return static_cast< double >( t.rgba[ ( static_cast< size_t >( j ) * film::kTintColumns + i ) * 4 + c ] ); };
	const double x = hNm * film::CosRefracted( theta ) / film::kTintStepNm;
	const int i    = std::clamp( static_cast< int >( std::floor( x ) ), 0, film::kTintColumns - 2 );
	const int j    = std::clamp( static_cast< int >( std::floor( deg / film::kTintAngleStep ) ), 0, film::kTintRows - 2 );
	for( int c = 0; c < 3; ++c )
	{
		const double slope = std::max( std::fabs( node( i + 1, j, c ) - node( i, j, c ) ), std::fabs( node( i + 1, j + 1, c ) - node( i, j + 1, c ) ) );
		const double step  = std::max( std::fabs( node( i, j + 1, c ) - node( i, j, c ) ), std::fabs( node( i + 1, j + 1, c ) - node( i + 1, j, c ) ) );
		const double size  = std::max( { std::fabs( node( i, j, c ) ), std::fabs( node( i + 1, j, c ) ), std::fabs( node( i, j + 1, c ) ), std::fabs( node( i + 1, j + 1, c ) ) } );
		e.want[ c ]        = E * std::max( rgb[ c ], 0.0 ) * clip;
		e.tol[ c ] = E * clip * ( tintBound() + slope * ( x + 1.0 ) * 8.0 * kFloat + step * 2.0 * kFloat + size * 16.0 * kFloat ) + e.want[ c ] * ( kEncode + 8.0 * kFloat ) + 1e-9;
	}
	return e;
}

/// The worst |measured - want| over every pixel, per channel, as a multiple
/// of the tolerance (the decode is monotone: the extremes decide).
double uniformError( const Floats& out, const Expect& e, double worstAbs[ 3 ] )
{
	double ratio = 0.0;
	for( int c = 0; c < 3; ++c )
	{
		float lo = std::numeric_limits< float >::infinity(), hi = -lo;
		bool nan = false;
		for( size_t p = 0; p < out.size() / 4; ++p )
		{
			const float v = out[ 4 * p + c ];
			nan |= v != v;
			lo = std::min( lo, v );
			hi = std::max( hi, v );
		}
		const double err = nan ? std::numeric_limits< double >::quiet_NaN() : std::max( std::fabs( linearOf( lo ) - e.want[ c ] ), std::fabs( linearOf( hi ) - e.want[ c ] ) );
		worstAbs[ c ]    = err;
		ratio            = worse( ratio, err / e.tol[ c ] );
	}
	return ratio;
}

/// The black-film discriminator: a pixel is black film when its brightest
/// channel is under the midpoint between the black film's brightest channel
/// and the least bright any film from h_cr to h_max is at this angle (both
/// from the harness's transfer matrix). Returns the threshold in linear
/// light at Exposure E; `gap` the ratio of the two levels.
double blackThreshold( double E, double hBlack, double thetaDeg, double& gap )
{
	static std::map< std::pair< double, double >, std::pair< double, double > > cache;
	const auto key = std::make_pair( hBlack, thetaDeg );
	if( cache.find( key ) == cache.end() )
	{
		double rgb[ 3 ];
		hp::tint( hBlack, thetaDeg * kPi / 180.0, rgb );
		const double blackTop = std::max( { rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] } );
		double least          = 1e9;
		for( double h = 30.0; h <= 4000.0; h += 1.0 )
		{
			hp::tint( h, thetaDeg * kPi / 180.0, rgb );
			least = std::min( least, std::max( { rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] } ) );
		}
		cache[ key ] = { blackTop, least };
	}
	const auto& v = cache[ key ];
	gap           = v.second / v.first;
	return E * 0.5 * ( v.first + v.second );
}

/// Per column, the black film's rows counted down from the top while they stay black.
std::vector< int > blackCounts( const Floats& out, int w, int h, double threshold )
{
	std::vector< int > counts( static_cast< size_t >( w ), 0 );
	for( int x = 0; x < w; ++x )
		for( int y = h - 1; y >= 0; --y )
		{
			const double top = std::max( { linAt( out, w, x, y, 0 ), linAt( out, w, x, y, 1 ), linAt( out, w, x, y, 2 ) } );
			if( !( top < threshold ) )
				break;
			++counts[ static_cast< size_t >( x ) ];
		}
	return counts;
}

/// The law's black-film rows at a raster: pixel row j (centre (j + 0.5) / h)
/// is black when it is above the front.
int predictedBlack( double yFront, int h )
{
	return h - static_cast< int >( std::floor( yFront * h + 0.5 ) );
}

/// How far, in rows, a pixel's centre may sit from the front and still go
/// either way: the thickness's relative error at the front (the G table's
/// bound, plus float) over |d ln G / dy| there. The bound is what --model
/// holds the table to between its nodes (measured worst 6.5e-6, at y ~ 1e-12,
/// the foot's log singularity; 1e-5 is that rounded up to a decade).
constexpr double kShapeRelative = 1e-5;
double frontBandRows( double yFront, int h )
{
	const double e  = 1e-5;
	const double dl = std::fabs( std::log( hp::G( std::min( yFront + e, 1.0 - 1e-12 ) ) ) - std::log( hp::G( std::max( yFront - e, 1e-12 ) ) ) ) / ( 2.0 * e );
	return h * ( kShapeRelative + 16.0 * kFloat ) / dl;
}

//===========================================================================
// --model (no GL)
//===========================================================================
int runModel( const Perturb& perturb )
{
	std::printf( "\n=== model: IAPWS against its own table; Airy against the transfer matrix; Pugh's last maximum and minimum; the colour table against the\n"
	             "    harness's own sum (nodes and between them); the G table; the master curve's fit; the film's clock; the conversions\n" );
	const int optics = perturb.opticsModel, drainage = perturb.drainageModel;

	// IAPWS R9-97 Table 3 at 0 C, 0.1 MPa: the density that gives the 589 nm
	// value, then the other two wavelengths at that density.
	{
		double lo = 990.0, hi = 1010.0;
		for( int i = 0; i < 200; ++i )
		{
			const double mid = 0.5 * ( lo + hi );
			( hp::iapws( 589.0, 0.0, mid ) < 1.334344 ? lo : hi ) = mid;
		}
		const double rho = 0.5 * ( lo + hi );
		const double e1 = std::fabs( hp::iapws( 226.5, 0.0, rho ) - 1.394527 ), e2 = std::fabs( hp::iapws( 1013.98, 0.0, rho ) - 1.326135 );
		Check( worse( e1, e2 ) <= 1.6e-6 && std::fabs( rho - 999.8438 ) <= 5e-3,
		       fmt( "IAPWS R9-97 Table 3 (0 C, 0.1 MPa): the density reproducing n(589 nm) = 1.334344 is %.4f kg/m3 (NIST 999.8438 at 0.01 C, "
		            "0.101325 MPa; tolerance 5e-3: the table's last digit 1.5e-3, 0.01 K 7e-4, 1.3 kPa 7e-4); at it n(226.5 nm) off by %.2g and "
		            "n(1013.98 nm) by %.2g (tolerance 1.6e-6: the release's one unit in the sixth decimal plus the 589 nm value's rounding)",
		            rho, e1, e2 ) );
		double worst = 0.0;
		for( double l = 360.0; l <= 830.0; l += 7.0 )
			worst = worse( worst, std::fabs( film::WaterIndex( l ) - hp::nWater( l ) ) );
		Check( worst <= 1e-12, fmt( "the plugin's n(lambda) at 20 C, 998.20715 kg/m3 against the harness's, 360-830 nm: worst %.2g (tolerance 1e-12, double)", worst ) );
	}

	// Airy (the plugin) against the transfer matrix (the harness), per
	// polarisation and unpolarised.
	{
		double worst = 0.0;
		for( double h : { 0.0, 3.0, 12.0, 30.0, 102.0, 333.0, 1000.0, 3999.0 } )
			for( double deg : { 0.0, 20.0, 45.0, 60.0 } )
				for( double l : { 380.0, 466.0, 550.0, 707.0, 830.0 } )
				{
					const double th = deg * kPi / 180.0, n = hp::nWater( l );
					const double want = 0.5 * ( hp::tmm( h, th, l, 0, n ) + hp::tmm( h, th, l, 1, n ) );
					worst             = worse( worst, std::fabs( film::Reflectance( h, th, l, optics ) - want ) );
				}
		Check( worst <= 1e-12, fmt( "Airy against the 2x2 transfer matrix, 8 thicknesses x 4 angles x 5 wavelengths: worst |dR| %.2g (tolerance 1e-12, double)", worst ) );
	}

	// Pugh Fig. 3.3(c): at 546 nm, the last maximum at 102 nm and the last
	// minimum at 204 nm of a thinning film.
	{
		double hMax = 0, rMax = -1, hMin = 0, rMin = 2;
		for( double h = 60.0; h <= 260.0; h += 0.001 )
		{
			const double r = film::Reflectance( h, 0.0, 546.0, optics );
			if( h < 150.0 && r > rMax )
			{
				rMax = r;
				hMax = h;
			}
			if( h >= 150.0 && r < rMin )
			{
				rMin = r;
				hMin = h;
			}
		}
		Check( std::fabs( hMax - 102.0 ) <= 0.5 && std::fabs( hMin - 204.0 ) <= 1.0,
		       fmt( "at 546 nm the plugin's reflectance peaks at %.3f nm and falls to its minimum at %.3f nm (Pugh Fig. 3.3c: 102 and 204 nm; "
		            "tolerance 0.5 nm for his integer, 1 nm for the minimum, which he gives as twice it)",
		            hMax, hMin ) );
	}

	// The colour table at its nodes, and between them.
	{
		const film::TintTable& table = film::Tints( optics );
		auto node = [ & ]( int i, int j, int c ) { return static_cast< double >( table.rgba[ ( static_cast< size_t >( j ) * film::kTintColumns + i ) * 4 + c ] ); };
		std::mt19937 rng( 7 );
		double worstNode = 0.0;
		for( int s = 0; s < 120; ++s )
		{
			const int i = static_cast< int >( rng() % film::kTintColumns ), j = static_cast< int >( rng() % film::kTintRows );
			const double th = j * 0.5 * kPi / 180.0;
			const double ct = std::sqrt( 1.0 - std::pow( std::sin( th ) / hp::nWater( 550.0 ), 2.0 ) );
			double rgb[ 3 ];
			hp::tint( i * 2.0 / ct, th, rgb );
			for( int c = 0; c < 3; ++c )
				worstNode = worse( worstNode, std::fabs( node( i, j, c ) - rgb[ c ] ) );
		}
		// Between nodes: the shader's bilinear, by hand, against the direct
		// sum; and the bound bilinear interpolation allows from the table's
		// own second differences.
		const double bound = curvatureBound( table );
		double worstBetween = 0.0;
		std::uniform_real_distribution< double > uh( 0.0, 4000.0 ), ua( 0.0, 60.0 );
		for( int s = 0; s < 4000; ++s )
		{
			const double h = s < 1000 ? uh( rng ) * 0.1 : uh( rng ), deg = ua( rng ), th = deg * kPi / 180.0;
			const double ct = film::CosRefracted( th );
			const double x = h * ct / 2.0, y = deg / 0.5;
			const int i = std::min( static_cast< int >( x ), film::kTintColumns - 2 ), j = std::min( static_cast< int >( y ), film::kTintRows - 2 );
			const double tx = x - i, ty = y - j;
			double rgb[ 3 ];
			hp::tint( h, th, rgb );
			for( int c = 0; c < 3; ++c )
			{
				const double lower = node( i, j, c ) + tx * ( node( i + 1, j, c ) - node( i, j, c ) );
				const double upper = node( i, j + 1, c ) + tx * ( node( i + 1, j + 1, c ) - node( i, j + 1, c ) );
				worstBetween       = worse( worstBetween, std::fabs( lower + ty * ( upper - lower ) - rgb[ c ] ) );
			}
		}
		Check( worstNode <= 2e-7 && worstBetween <= bound && bound <= 1.5e-4,
		       fmt( "colour table (%d x %d, built in %.3f s): at 120 nodes off the harness's transfer-matrix sum by %.2g (tolerance 2e-7: the float "
		            "store of values <= 1); between nodes, 4000 random films (1000 under 400 nm), off by %.3g against the bilinear bound %.3g from the "
		            "table's own second differences (1/8 of the worst, x 1.25 for the higher terms; it must stay under the spec's 1.5e-4)",
		            film::kTintColumns, film::kTintRows, table.seconds, worstNode, worstBetween, bound ) );
		std::printf( "      kTintBound (used by --colour) is this bound: %.4g\n", bound );
	}

	// The G table against the harness's G (bisection), and the thickness it
	// gives between nodes over the film's life.
	{
		const std::vector< float > table = film::ShapeTable( drainage );
		// Only where a film can be between h_cr and h_max at some age to 10
		// minutes at Drainage 10 and some Thickness: A from 5e-11 to 2e-6 m.
		auto relevant = []( double g ) { return g * 2e-6 >= 30e-9 && g * 5e-11 <= 4000e-9; };
		double worstNode = 0.0;
		int nodes        = 0;
		for( int i = 1; i < film::kShapeSize; ++i )
		{
			const double w8 = std::pow( static_cast< double >( i ) / ( film::kShapeSize - 1 ) * std::pow( 0.5, 0.125 ), 8.0 );
			const double g0 = hp::G( w8 ), g1 = hp::GComplement( w8 );
			if( relevant( g0 ) )
			{
				worstNode = worse( worstNode, std::fabs( table[ i ] / g0 - 1.0 ) );
				++nodes;
			}
			if( relevant( g1 ) )
			{
				worstNode = worse( worstNode, std::fabs( table[ film::kShapeSize + i ] / g1 - 1.0 ) );
				++nodes;
			}
		}
		const double scale = ( film::kShapeSize - 1 ) / std::pow( 0.5, 0.125 );
		double worstRel = 0.0;
		std::mt19937 rng( 11 );
		std::uniform_real_distribution< double > uy( 0.0, 1.0 );
		int heights = 0;
		for( int s = 0; s < 20000; ++s )
		{
			// v: the distance from the nearer end (y, or 1 - y), weighted to
			// the ends where the table's warp matters.
			const int row  = s % 2;
			const double v = s < 10000 ? 0.5 * std::pow( uy( rng ), 8.0 ) : 0.5 * uy( rng );
			// A float y = X.y + d is 0 or at least ~1e-12 (the spacing of a
			// float near the lowest pixel centre a texture can have, 0.5 /
			// 16384): nearer the ends than that the table is never read.
			if( v < 1e-12 )
				continue;
			const double x = std::sqrt( std::sqrt( std::sqrt( v ) ) ) * scale;
			const int i = std::min( static_cast< int >( x ), film::kShapeSize - 2 );
			const double t = x - i, lo = table[ row * film::kShapeSize + i ], hi = table[ row * film::kShapeSize + i + 1 ];
			const double g = lo + t * ( hi - lo ), want = row == 0 ? hp::G( v ) : hp::GComplement( v );
			if( !relevant( want ) )
				continue;
			worstRel = worse( worstRel, std::fabs( g / want - 1.0 ) );
			++heights;
		}
		Check( worstNode <= 1e-6 && worstRel <= kShapeRelative,
		       fmt( "G table: at its %d nodes where a film can be 30 nm - 4 um off the harness's bisection inverse by %.2g relative (tolerance 1e-6: "
		            "float store and pow); between them, %d such heights by %.3g relative (the bound --drainage uses, kShapeRelative = %.1g)",
		            nodes, worstNode, heights, worstRel, kShapeRelative ) );
	}

	// The master curve: the fit against the digitised points, and the
	// constraints at u = 1.
	{
		const double u[ 10 ] = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 3.5 };
		const double y[ 10 ] = { 0.995, 0.982, 0.902, 0.80, 0.679, 0.58, 0.42, 0.304, 0.205, 0.130 };
		double ss = 0.0, worst = 0.0, inv = 0.0;
		for( int i = 0; i < 10; ++i )
		{
			const double r = film::MasterCurve( u[ i ] ) - y[ i ];
			ss += r * r;
			worst = worse( worst, std::fabs( r ) );
			inv   = worse( inv, std::fabs( film::MasterInverse( y[ i ] ) - hp::finv( y[ i ] ) ) / hp::finv( y[ i ] ) );
		}
		const double rms = std::sqrt( ss / 10.0 );
		const double d1  = ( film::MasterCurve( 1.0 + 1e-6 ) - film::MasterCurve( 1.0 - 1e-6 ) ) / 2e-6;
		Check( rms <= 0.0091 && worst <= 0.0174 && std::fabs( film::MasterCurve( 1.0 ) - 0.8 ) <= 1e-4 && std::fabs( d1 + 0.44 ) <= 1e-3 && inv <= 1e-9,
		       fmt( "master curve against Monier et al.'s Fig. 3b (10 digitised points): rms %.4f, worst %.4f (the spec's 0.009 and 0.017, rounded up); "
		            "f(1) = %.5f (0.8 +- 1e-4), f'(1) = %.4f (-0.44 +- 1e-3); the closed-form inverse against bisection, worst %.2g relative (1e-9)",
		            rms, worst, film::MasterCurve( 1.0 ), d1, inv ) );
	}

	// The film's clock: 24, 60 and 144 fps of whole-microsecond intervals
	// reach the same state at the times they share, through a burst and a
	// refill.
	{
		film::Settings s;
		s.drainage     = 10.0;
		s.reach        = 0.3;
		s.slowMotion   = 1000.0;
		s.refillSeconds = 0.5;
		s.drainageModel = drainage;
		const double rates[ 3 ] = { 24.0, 60.0, 144.0 };
		std::map< int, std::tuple< int, int64_t, double, int64_t, int64_t, uint32_t > > seen[ 3 ];
		for( int r = 0; r < 3; ++r )
		{
			film::Clock c;
			c.Restart();
			int64_t last = 0;
			for( int k = 0; k <= static_cast< int >( rates[ r ] * 40.0 ); ++k )
			{
				const int64_t now = std::llround( k / rates[ r ] * 1e6 );
				c.Advance( k == 0 ? 0 : now - last, s, false, false );
				last = now;
				if( k % static_cast< int >( rates[ r ] / 12.0 ) == 0 )
					seen[ r ][ static_cast< int >( k * 12 / static_cast< int >( rates[ r ] ) ) ]
					    = std::make_tuple( static_cast< int >( c.phase ), c.age, c.tau, c.burstMicros, c.emptyMicros, c.generation );
			}
		}
		int differ = 0, bursts = 0;
		for( const auto& e : seen[ 0 ] )
		{
			differ += e.second != seen[ 1 ].at( e.first ) || e.second != seen[ 2 ].at( e.first );
			bursts += std::get< 0 >( e.second ) != 0;
		}
		Check( differ == 0 && bursts > 0,
		       fmt( "the film's clock at 24, 60 and 144 fps, 40 s through bursts and refills: %d of %zu shared times differ in phase, age, tau, "
		            "burst or empty time, or generation (bit-exact); %d of them not draining",
		            differ, seen[ 0 ].size(), bursts ) );
	}

	// The conversions, against the spec's restatement.
	{
		double worst = 0.0;
		for( float v = 0.0f; v <= 1.0f; v += 0.0625f )
		{
			worst = worse( worst, std::fabs( ThicknessFromParam( v ) - hThickness( v ) ), std::fabs( DrainageFromParam( v ) - hDrainage( v ) ) / hDrainage( v ),
			               std::fabs( SwirlFromParam( v ) - hSwirl( v ) ), std::fabs( SlowMotionFromParam( v ) - hSlow( v ) ) / hSlow( v ),
			               std::fabs( RefillFromParam( v ) - hRefill( v ) ), std::fabs( AngleFromParam( v ) - hAngle( v ) ),
			               std::fabs( ExposureFromParam( v ) - hExposure( v ) ) );
		}
		const bool nanSafe = ThicknessFromParam( std::numeric_limits< float >::quiet_NaN() ) == 0.2;
		Check( worst <= 1e-12 && nanSafe, fmt( "control conversions against the spec's ranges, 17 points each: worst %.2g (1e-12); NaN reads as the bottom of the range: %s",
		                                       worst, nanSafe ? "yes" : "NO" ) );
	}
	return Verdict();
}

//===========================================================================
// --colour: a uniform film against the harness's transfer-matrix tint
//===========================================================================
int runColour( const Perturb& perturb )
{
	std::printf( "\n=== colour: a uniform film (the forceNm seam) of 18 thicknesses x 4 angles over a white clip, every pixel against Exposure x the\n"
	             "    harness's own tint (CIE 1931 2-degree CMFs x D65 x the transfer matrix's reflectance, 1 nm, white-corrected)\n" );
	const double hs[]  = { 0.0, 5.0, 12.0, 25.0, 60.0, 102.0, 150.0, 204.0, 280.0, 350.0, 450.0, 600.0, 800.0, 1000.0, 1500.0, 2200.0, 3000.0, 3900.0 };
	const double degs[] = { 0.0, 23.25, 45.0, 59.75 };
	for( const Raster& raster : kRasters )
	{
		const Floats white = flatCard( raster.w, raster.h, 1.0f, 1.0f, 1.0f );
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb, &white ) )
			return 1;
		double worstRatio = 0.0, worstAbs = 0.0, worstTol = 0.0, atH = 0.0, atDeg = 0.0;
		int samples = 0;
		for( const double deg : degs )
			for( const double hNm : hs )
			{
				double rgb[ 3 ];
				hp::tint( hNm, deg * kPi / 180.0, rgb );
				const double top = std::max( { rgb[ 0 ], rgb[ 1 ], rgb[ 2 ], 1e-3 } );
				const float pE   = pExposure( std::min( 6.0, 0.9 / top ) );
				const float pA   = pAngle( deg );
				rig.Set( PT_EXPOSURE, pE );
				rig.Set( PT_ANGLE, pA );
				rig.plugin.HooksForTest().forceNm = static_cast< double >( static_cast< float >( hNm ) );
				if( !rig.Render( 1 ) )
					return 1;
				const Expect e = expectFilm( hNm, hAngle( pA ), hExposure( pE ), 1.0 );
				double abs[ 3 ];
				const double ratio = uniformError( rig.Output(), e, abs );
				++samples;
				if( !( ratio <= worstRatio ) )
				{
					worstRatio = ratio;
					atH        = hNm;
					atDeg      = deg;
					worstAbs   = std::max( { abs[ 0 ], abs[ 1 ], abs[ 2 ] } );
					worstTol   = std::min( { e.tol[ 0 ], e.tol[ 1 ], e.tol[ 2 ] } );
				}
			}
		Check( worstRatio <= 1.0, fmt( "%4dx%-4d %d films, every pixel: the worst is %.3f of its tolerance (%.3g off against %.3g, at %.0f nm, %.2f deg); "
		                               "tolerance per channel: Exposure x (the table's interpolation bound %.3g, from its own second differences, "
		                               "+ the column slope x the position's float error) + the encode's 2^-17",
		                               raster.w, raster.h, samples, worstRatio, worstAbs, worstTol, atH, atDeg, tintBound() ) );
	}
	return Verdict();
}

//===========================================================================
// --black: below h_cr the film is the black film's thickness
//===========================================================================
/// The harness's law: the thickness at height y (nm) at drainage time tau
/// for a film `um` micrometres thick at mid-height at birth.
double lawNm( double y, double tau, double um )
{
	return hp::A( tau, hp::t0( um * 1e-6 ) ) * hp::G( y ) * 1e9;
}
/// The drainage time at which the front stands at yf.
double tauForFront( double yf, double um )
{
	return hp::t0( um * 1e-6 ) + hp::T( hp::kHcr ) * hp::finv( yf );
}

int runBlack( const Perturb& perturb )
{
	std::printf( "\n=== black: where the law is under h_cr = 30 nm the film is the black film -- 12 nm Common, 4 nm Newton -- row by row against\n"
	             "    the harness's tint of that thickness; where it is over, the law's own thickness (no jump)\n" );
	struct Case
	{
		int option;
		double blackNm, deg;
		const char* name;
	};
	const Case cases[] = { { 0, 12.0, 0.0, "Common" }, { 1, 4.0, 40.0, "Newton" } };
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const Floats white = flatCard( w, h, 1.0f, 1.0f, 1.0f );
		for( const Case& c : cases )
		{
			Rig rig;
			if( !startRig( rig, w, h, perturb, &white, 10.0 ) )
				return 1;
			rig.Set( PT_BLACK_FILM, c.option );
			const float pA = pAngle( c.deg );
			rig.Set( PT_ANGLE, pA );
			const double um = hThickness( pThickness( 1.5 ) ), D = hDrainage( pDrainage( 4.0 ) );
			const int k      = static_cast< int >( std::ceil( tauForFront( 0.55, um ) / D * 10.0 ) );
			if( !rig.Render( k + 1 ) )
				return 1;
			const double tau = D * static_cast< double >( ageAt( k, 10.0 ) ) * 1e-6;
			const Floats out = rig.Output();
			const double E   = hExposure( pExposure( 6.0 ) );
			const Expect e   = expectFilm( c.blackNm, hAngle( pA ), E, 1.0 );
			int blackRows = 0, wrongRows = 0;
			double worst = 0.0;
			for( int y = 0; y < h; ++y )
			{
				const double law = lawNm( ( y + 0.5 ) / h, tau, um );
				if( !( law < hp::kHcr * 1e9 * ( 1.0 - 1e-4 ) ) )
					continue;
				++blackRows;
				bool wrong = false;
				for( int x = 0; x < w; ++x )
					for( int ch = 0; ch < 3; ++ch )
					{
						const double err = std::fabs( linAt( out, w, x, y, ch ) - e.want[ ch ] ) / e.tol[ ch ];
						worst            = worse( worst, err );
						wrong |= !( err <= 1.0 );
					}
				wrongRows += wrong;
			}
			Check( blackRows >= h / 4 && wrongRows == 0,
			       fmt( "%4dx%-4d %s at %.0f deg: %d rows where the law is under 30 nm (by 1e-4, the law's own float error well inside), %d of them "
			            "with any pixel not the %.0f nm film; worst %.3f of the tolerance (Exposure x the table's bound + float + encode)",
			            w, h, c.name, c.deg, blackRows, wrongRows, c.blackNm, worst ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --drainage: the black front's height, column by column, against the law
//===========================================================================
int runDrainage( const Perturb& perturb )
{
	std::printf( "\n=== drainage: Swirl 0, 10 fps; the black film's rows in every column against the law's front y_f = f( ( tau - t0 ) / T( h_cr ) ),\n"
	             "    tau = Drainage x age; rows counted from the top, which must be the only black ones (every column: the edges included)\n" );
	struct Setting
	{
		double um, D;
	};
	const Setting settings[] = { { 1.5, 4.0 }, { 3.0, 4.0 }, { 1.5, 10.0 } };
	const double targets[]   = { 0.95, 0.75, 0.5, 0.3 };
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const Floats white = flatCard( w, h, 1.0f, 1.0f, 1.0f );
		double gap         = 0.0;
		const double E     = hExposure( pExposure( 6.0 ) );
		const double threshold = blackThreshold( E, 12.0, 0.0, gap );
		for( const Setting& s : settings )
		{
			Rig rig;
			if( !startRig( rig, w, h, perturb, &white, 10.0 ) )
				return 1;
			rig.Set( PT_THICKNESS, pThickness( s.um ) );
			rig.Set( PT_DRAINAGE, pDrainage( s.D ) );
			const double um = hThickness( pThickness( s.um ) ), D = hDrainage( pDrainage( s.D ) );
			std::string lines;
			int badColumns = 0, strayColumns = 0;
			for( const double target : targets )
			{
				const int k = static_cast< int >( std::ceil( tauForFront( target, um ) / D * 10.0 ) );
				if( rig.frame <= k && !rig.Render( k + 1 - rig.frame ) )
					return 1;
				const double tau  = D * static_cast< double >( ageAt( k, 10.0 ) ) * 1e-6;
				const double yf   = hp::front( tau, hp::t0( um * 1e-6 ) );
				const double band = frontBandRows( yf, h ) + 16.0 * kFloat * h;
				int lo = 0, hi = 0;//the rows that must be black, and that may be
				for( int y = 0; y < h; ++y )
				{
					lo += ( y + 0.5 ) > yf * h + band;
					hi += ( y + 0.5 ) > yf * h - band;
				}
				const Floats out               = rig.Output();
				const std::vector< int > top   = blackCounts( out, w, h, threshold );
				int least = h, most = 0;
				for( int x = 0; x < w; ++x )
				{
					int total = 0;
					for( int y = 0; y < h; ++y )
						total += std::max( { linAt( out, w, x, y, 0 ), linAt( out, w, x, y, 1 ), linAt( out, w, x, y, 2 ) } ) < threshold;
					strayColumns += total != top[ x ];
					badColumns += top[ x ] < lo || top[ x ] > hi;
					least = std::min( least, top[ x ] );
					most  = std::max( most, top[ x ] );
				}
				lines += fmt( " y_f %.4f: %d-%d rows (law %d%s)", yf, least, most, predictedBlack( yf, h ), lo == hi ? "" : fmt( ", %d-%d allowed", lo, hi ).c_str() );
			}
			Check( badColumns == 0 && strayColumns == 0,
			       fmt( "%4dx%-4d %.1f um, Drainage %.0f:%s; %d column-measurements off the law, %d with black below the front (a pixel is black under "
			            "the midpoint of the 12 nm film and the dimmest film over 30 nm, %.1fx apart; a row may go either way only within the G table's "
			            "%.0e relative error of the front)",
			            w, h, s.um, s.D, lines.c_str(), badColumns, strayColumns, gap, kShapeRelative ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --rate: 24, 60 and 144 fps agree bit for bit at the times they share
//===========================================================================
void stirred( Rig& rig )
{
	rig.Set( PT_THICKNESS, pThickness( 0.2 ) );
	rig.Set( PT_DRAINAGE, pDrainage( 10.0 ) );
	rig.Set( PT_SWIRL, pSwirl( 10.0 ) );
}

int runRate( const Perturb& perturb )
{
	std::printf( "\n=== rate: Swirl 10 mm/s, a film draining at Drainage 10: renders at 24, 60 and 144 fps compared at 1, 2 and 3 s (bit-exact: the\n"
	             "    film's age is whole microseconds and the stirring steps at 720 Hz of it, whatever the frame rate)\n" );
	for( const Raster& raster : kRasters )
	{
		const double rates[ 3 ] = { 24.0, 60.0, 144.0 };
		Rig rigs[ 3 ];
		const Floats card = lookCard( raster.w, raster.h );
		for( int r = 0; r < 3; ++r )
		{
			if( !startRig( rigs[ r ], raster.w, raster.h, perturb, &card, rates[ r ] ) )
				return 1;
			stirred( rigs[ r ] );
		}
		size_t differ = 0;
		int64_t steps = 0;
		for( int second = 1; second <= 3; ++second )
		{
			Floats out[ 3 ];
			for( int r = 0; r < 3; ++r )
			{
				const int k = static_cast< int >( rates[ r ] ) * second;
				if( !rigs[ r ].Render( k + 1 - rigs[ r ].frame ) )
					return 1;
				out[ r ] = rigs[ r ].Output();
			}
			differ += differing( out[ 0 ], out[ 1 ] ) + differing( out[ 0 ], out[ 2 ] );
			steps = rigs[ 1 ].plugin.StepsRun();
		}
		Check( differ == 0 && steps == 3 * film::kStepRate,
		       fmt( "%4dx%-4d 24 and 144 fps against 60 at 1, 2 and 3 s: %zu values differ (bit-exact); %lld advection steps by 3 s (720 a second of age: %d)",
		            raster.w, raster.h, differ, static_cast< long long >( steps ), 3 * film::kStepRate ) );
	}
	return Verdict();
}

//===========================================================================
// --clock: Resolume's millisecond clock at 4.99e8, and a stalled clock
//===========================================================================
int runClock( const Perturb& perturb )
{
	std::printf( "\n=== clock: a host clock in milliseconds from 4.99e8 (Resolume's, measured) against one in seconds from 0, stirred and draining\n"
	             "    (bit-exact at frames 50, 110 and 170 of 60 fps); then the host's time held still for 30 frames: the film holds still (bit-exact)\n" );
	for( const Raster& raster : kRasters )
	{
		const Floats card = lookCard( raster.w, raster.h );
		Rig base, late;
		late.timeOffset = 4.99e8;
		late.timeScale  = 1000.0;
		late.clockUnit  = 0.001;
		if( !startRig( base, raster.w, raster.h, perturb, &card ) || !startRig( late, raster.w, raster.h, perturb, &card ) )
			return 1;
		stirred( base );
		stirred( late );
		// Frames 50, 110 and 170: times a float at 4.99e5 s cannot hold
		// (its spacing there is 1/32 s; a whole second it can).
		size_t differ = 0;
		for( const int k : { 50, 110, 170 } )
		{
			if( !base.Render( k + 1 - base.frame ) || !late.Render( k + 1 - late.frame ) )
				return 1;
			differ += differing( base.Output(), late.Output() );
		}
		const Floats before = base.Output();
		const double held   = base.timeOffset + base.timeScale * base.Seconds( base.frame - 1 );
		base.beforeFrame    = [ & ]( int f ) { base.timeOffset = held - base.timeScale * base.Seconds( f ); };
		if( !base.Render( 30 ) )
			return 1;
		const size_t moved = differing( before, base.Output() );
		Check( differ == 0 && moved == 0,
		       fmt( "%4dx%-4d milliseconds from 4.99e8 against seconds from 0: %zu values differ at 0.83, 1.83 and 2.83 s (bit-exact); 30 frames at one host "
		            "time: %zu values moved (bit-exact)",
		            raster.w, raster.h, differ, moved ) );
	}
	return Verdict();
}

//===========================================================================
// --swirl: the black film where the true flow carries it
//===========================================================================
int runSwirl( const Perturb& perturb )
{
	std::printf( "\n=== swirl: Swirl 10 mm/s for 4 s of film (2880 steps), Thickness 0.2 um, Drainage 10: a lattice of pixels traced back through the\n"
	             "    spec's six modes in double (the C library's sine), black where the traced origin is above the law's front; a pixel may differ\n"
	             "    only within the scheme's error of the front: linear interpolation's numerical diffusion (|v| dx / 2 per unit time, each axis)\n"
	             "    x the age x the traced field's largest second difference, + the composite's own interpolation, + float\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const double aspect = static_cast< double >( w ) / h;
		const Floats white  = flatCard( w, h, 1.0f, 1.0f, 1.0f );
		Rig rig;
		if( !startRig( rig, w, h, perturb, &white ) )
			return 1;
		stirred( rig );
		const int K = 240;
		if( !rig.Render( K + 1 ) )
			return 1;
		const double um = hThickness( pThickness( 0.2 ) ), D = hDrainage( pDrainage( 10.0 ) ), swirl = hSwirl( pSwirl( 10.0 ) ) * 1e-3;
		const int64_t age   = ageAt( K, 60.0 );
		const int64_t steps = ( age * film::kStepRate + 500000 ) / 1000000;
		const double tau    = D * static_cast< double >( age ) * 1e-6;
		const double yf     = hp::front( tau, hp::t0( um * 1e-6 ) );
		const Floats out    = rig.Output();
		const Floats field  = rig.Field();
		const int gw = rig.plugin.GridWidth(), gh = rig.plugin.GridHeight();
		const hp::Flow flow = hp::flowFor( 1, aspect );

		// The pixel lattice.
		const int NX = 96, NY = 54;
		std::vector< double > zeta( static_cast< size_t >( NX ) * NY );
		std::vector< int > px( zeta.size() ), py( zeta.size() );
		for( int j = 0; j < NY; ++j )
			for( int i = 0; i < NX; ++i )
			{
				const size_t n = static_cast< size_t >( j ) * NX + i;
				px[ n ]        = static_cast< int >( ( i + 0.5 ) * w / NX );
				py[ n ]        = static_cast< int >( ( j + 0.5 ) * h / NY );
			}
		parallelFor( static_cast< int >( zeta.size() ), [ & ]( int n ) {
			double X = ( px[ n ] + 0.5 ) / w, Y = ( py[ n ] + 0.5 ) / h;
			hp::traceBack( flow, swirl, aspect, steps, X, Y );
			zeta[ n ] = Y;
		} );

		// The traced field's second differences at grid spacing, at the end
		// and halfway, on a 32 x 18 lattice of grid cells: zeta at the cell
		// and its four neighbours.
		const double dx = 1.0 / gw, dy = 1.0 / gh;
		const int CX = 32, CY = 18;
		std::vector< std::array< double, 5 > > end( static_cast< size_t >( CX ) * CY ), half( end.size() );
		std::vector< int > cx( end.size() ), cy( end.size() );
		for( int j = 0; j < CY; ++j )
			for( int i = 0; i < CX; ++i )
			{
				const size_t n = static_cast< size_t >( j ) * CX + i;
				cx[ n ]        = 1 + static_cast< int >( ( i + 0.5 ) * ( gw - 2 ) / CX );
				cy[ n ]        = 1 + static_cast< int >( ( j + 0.5 ) * ( gh - 2 ) / CY );
			}
		const double offX[ 5 ] = { 0, 1, -1, 0, 0 }, offY[ 5 ] = { 0, 0, 0, 1, -1 };
		parallelFor( static_cast< int >( end.size() ) * 10, [ & ]( int m ) {
			const size_t n = static_cast< size_t >( m / 10 );
			const int s = m % 5, at = ( m / 5 ) % 2;
			double X = ( cx[ n ] + 0.5 + offX[ s ] ) * dx, Y = ( cy[ n ] + 0.5 + offY[ s ] ) * dy;
			hp::traceBack( flow, swirl, aspect, at == 0 ? steps : steps / 2, X, Y );
			( at == 0 ? end : half )[ n ][ s ] = Y;
		} );
		double sx = 0.0, sy = 0.0, fieldErr = 0.0;
		for( size_t n = 0; n < end.size(); ++n )
		{
			for( const auto* z : { &end[ n ], &half[ n ] } )
			{
				sx = worse( sx, std::fabs( ( *z )[ 1 ] - 2.0 * ( *z )[ 0 ] + ( *z )[ 2 ] ) );
				sy = worse( sy, std::fabs( ( *z )[ 3 ] - 2.0 * ( *z )[ 0 ] + ( *z )[ 4 ] ) );
			}
			const double dTrue = end[ n ][ 0 ] - ( cy[ n ] + 0.5 ) * dy;
			fieldErr           = worse( fieldErr, std::fabs( field[ static_cast< size_t >( cy[ n ] ) * gw + cx[ n ] ] - dTrue ) );
		}
		// The flow's largest normalised speeds, from its coefficients' bound.
		double vx = 0.0, vy = 0.0;
		const double W = 0.025 * aspect, H = 0.025;
		for( int k = 0; k < 6; ++k )
		{
			const double c = swirl * flow.w[ k ] / flow.bound * kPi / ( W * H );
			vx += c * flow.q[ k ];
			vy += c * flow.p[ k ];
		}
		const double seconds   = static_cast< double >( age ) * 1e-6;
		const double diffusion = seconds * ( vx * sx / dx + vy * sy / dy ) / 2.0;
		const double floats    = static_cast< double >( steps ) * 8.0 * kFloat + 64.0 * kFloat;
		const double band      = diffusion + ( sx + sy ) / 8.0 + floats + frontBandRows( yf, h ) / h;

		double gap = 0.0;
		const double threshold = blackThreshold( hExposure( pExposure( 6.0 ) ), 12.0, 0.0, gap );
		int outside = 0, inside = 0, crossed = 0;
		double widest = 0.0;
		for( size_t n = 0; n < zeta.size(); ++n )
		{
			const double Y     = ( py[ n ] + 0.5 ) / h;
			const bool want    = zeta[ n ] > yf;
			const double top   = std::max( { linAt( out, w, px[ n ], py[ n ], 0 ), linAt( out, w, px[ n ], py[ n ], 1 ), linAt( out, w, px[ n ], py[ n ], 2 ) } );
			const bool black   = top < threshold;
			crossed += ( Y > yf ) != want;
			if( black == want )
				continue;
			const double off = std::fabs( zeta[ n ] - yf );
			widest           = worse( widest, off );
			( off <= band ? inside : outside )++;
		}
		Check( outside == 0 && crossed > static_cast< int >( zeta.size() ) / 50,
		       fmt( "%4dx%-4d %zu pixels traced: the flow carried %d across the front (y_f %.4f); %d disagree with the trace beyond the band %.4f of the "
		            "frame height, %d within it (the farthest %.4f from the front); the band: diffusion %.4f + interpolation %.4f + float %.1e",
		            w, h, zeta.size(), crossed, yf, outside, band, inside, widest, diffusion, ( sx + sy ) / 8.0, floats ) );
		Check( fieldErr <= diffusion + floats,
		       fmt( "%4dx%-4d the plugin's displacement grid (%dx%d, read back) at %zu cells against the traced displacement: worst %.4f of the frame "
		            "height (tolerance: the diffusion estimate + float, %.4f)",
		            w, h, gw, gh, end.size(), fieldErr, diffusion + floats ) );
	}
	return Verdict();
}

//===========================================================================
// --resize: the same aspect keeps the film bit for bit; another resamples it
//===========================================================================
/// The plugin's fieldAt in double: cell-centred bilinear, clamped.
double fieldSample( const Floats& f, int fw, int fh, double X, double Y )
{
	const double gx = X * fw - 0.5, gy = Y * fh - 0.5;
	const double lx = std::clamp( std::floor( gx ), 0.0, static_cast< double >( fw - 1 ) ), ly = std::clamp( std::floor( gy ), 0.0, static_cast< double >( fh - 1 ) );
	const double tx = std::clamp( gx - lx, 0.0, 1.0 ), ty = std::clamp( gy - ly, 0.0, 1.0 );
	const int x0 = static_cast< int >( lx ), y0 = static_cast< int >( ly );
	const int x1 = std::min( x0 + 1, fw - 1 ), y1 = std::min( y0 + 1, fh - 1 );
	auto at      = [ & ]( int x, int y ) { return static_cast< double >( f[ static_cast< size_t >( y ) * fw + x ] ); };
	const double lower = at( x0, y0 ) + tx * ( at( x1, y0 ) - at( x0, y0 ) );
	const double upper = at( x0, y1 ) + tx * ( at( x1, y1 ) - at( x0, y1 ) );
	return lower + ty * ( upper - lower );
}

int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: halfway through, the raster halves (the same aspect): the film carries on bit for bit as if it had always been the\n"
	             "    smaller one; the raster turns square: the grid is resampled (against the harness's bilinear of the grid read back before) and\n"
	             "    the picture's black film is where that resampled field puts it, at the film's continued age\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const Floats big = lookCard( 2 * w, 2 * h ), small = lookCard( w, h );
		Rig a, b;
		if( !startRig( a, 2 * w, 2 * h, perturb, &big ) || !startRig( b, w, h, perturb, &small ) )
			return 1;
		stirred( a );
		stirred( b );
		if( !a.Render( 60 ) || !a.Resize( w, h, &small ) || !a.Render( 30 ) || !b.Render( 90 ) )
			return 1;
		const size_t same = differing( a.Output(), b.Output() );

		const Floats white = flatCard( w, h, 1.0f, 1.0f, 1.0f ), square = flatCard( h, h, 1.0f, 1.0f, 1.0f );
		Rig c;
		if( !startRig( c, w, h, perturb, &white ) )
			return 1;
		stirred( c );
		if( !c.Render( 240 ) )
			return 1;
		c.Set( PT_SWIRL, 0.0f );
		if( !c.Render( 1 ) )
			return 1;
		const Floats before = c.Field();
		const int ow = c.plugin.GridWidth(), oh = c.plugin.GridHeight();
		const uint32_t generation = c.plugin.FilmClock().generation;
		if( !c.Resize( h, h, &square ) || !c.Render( 1 ) )
			return 1;
		const Floats after = c.Field();
		const int nw = c.plugin.GridWidth(), nh = c.plugin.GridHeight();
		double worstField = 0.0, step = 0.0;
		for( int y = 0; y < oh; ++y )
			for( int x = 0; x + 1 < ow; ++x )
				step = std::max( step, std::fabs( static_cast< double >( before[ static_cast< size_t >( y ) * ow + x + 1 ] - before[ static_cast< size_t >( y ) * ow + x ] ) ) );
		for( int y = 0; y + 1 < oh; ++y )
			for( int x = 0; x < ow; ++x )
				step = std::max( step, std::fabs( static_cast< double >( before[ static_cast< size_t >( y + 1 ) * ow + x ] - before[ static_cast< size_t >( y ) * ow + x ] ) ) );
		Floats resampled( static_cast< size_t >( nw ) * nh );
		for( int y = 0; y < nh; ++y )
			for( int x = 0; x < nw; ++x )
			{
				const double want = fieldSample( before, ow, oh, ( x + 0.5 ) / nw, ( y + 0.5 ) / nh );
				resampled[ static_cast< size_t >( y ) * nw + x ] = static_cast< float >( want );
				if( static_cast< size_t >( nw ) * nh == after.size() )
					worstField = worse( worstField, std::fabs( after[ static_cast< size_t >( y ) * nw + x ] - want ) );
			}
		// The grid position's float error (~8 roundings of a coordinate up to
		// 512) times the largest step between cells, + the interpolation's own.
		const double fieldTol = 512.0 * 8.0 * kFloat * step + 16.0 * kFloat;

		const double um = hThickness( pThickness( 0.2 ) ), D = hDrainage( pDrainage( 10.0 ) );
		const double tau  = D * static_cast< double >( ageAt( c.frame - 1, 60.0 ) ) * 1e-6;
		const double yf   = hp::front( tau, hp::t0( um * 1e-6 ) );
		const double band = frontBandRows( yf, h ) / h + 512.0 * 8.0 * kFloat * step + 64.0 * kFloat;
		double gap = 0.0;
		const double threshold = blackThreshold( hExposure( pExposure( 6.0 ) ), 12.0, 0.0, gap );
		const Floats out = c.Output();
		int wrong = 0, blacks = 0;
		for( int y = 0; y < h; ++y )
			for( int x = 0; x < h; ++x )
			{
				const double X = ( x + 0.5 ) / h, Y = ( y + 0.5 ) / h;
				const double zeta = std::clamp( Y + fieldSample( resampled, nw, nh, X, Y ), 0.0, 1.0 );
				const bool black  = std::max( { linAt( out, h, x, y, 0 ), linAt( out, h, x, y, 1 ), linAt( out, h, x, y, 2 ) } ) < threshold;
				blacks += black;
				wrong += black != ( zeta > yf ) && std::fabs( zeta - yf ) > band;
			}
		const bool kept = c.plugin.FilmClock().generation == generation;
		Check( same == 0, fmt( "%4dx%-4d %dx%d for 60 frames, then %dx%d for 30, against %dx%d throughout: %zu values differ (bit-exact)", w, h, 2 * w, 2 * h, w, h, w, h, same ) );
		Check( kept && after.size() == static_cast< size_t >( nw ) * nh && worstField <= fieldTol && wrong == 0 && blacks > 0,
		       fmt( "%4dx%-4d to %dx%d: the same film (generation kept: %s); grid %dx%d -> %dx%d off the harness's bilinear resample of the grid read "
		            "back by %.2g (tolerance %.2g: float position x the largest cell step %.3f); %d of %d pixels' black film (%d black) not where the "
		            "resampled field and the continued age put it, beyond %.1e of the front",
		            w, h, h, h, kept ? "yes" : "NO", ow, oh, nw, nh, worstField, fieldTol, step, wrong, h * h, blacks, band ) );
	}
	return Verdict();
}

//===========================================================================
// --burst: the hole runs at the Taylor-Culick speed from the predicted point
//===========================================================================
int runBurst( const Perturb& perturb )
{
	std::printf( "\n=== burst: a uniform film (12, 300, 2000 nm), Pop: the open pixels (all three channels 0) against a circle of radius\n"
	             "    sqrt( 2 gamma / ( rho h ) ) x t / Slow Motion about the harness's nucleation point (the generation's hash; the front's height)\n" );
	struct Case
	{
		double h, slow;
	};
	const Case cases[] = { { 12.0, 1000.0 }, { 300.0, 200.0 }, { 2000.0, 100.0 } };
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const double aspect = static_cast< double >( w ) / h, W = 0.025 * aspect, H = 0.025;
		const Floats white  = flatCard( w, h, 1.0f, 1.0f, 1.0f );
		for( const Case& c : cases )
		{
			Rig rig;
			if( !startRig( rig, w, h, perturb, &white ) )
				return 1;
			rig.plugin.HooksForTest().forceNm = c.h;
			rig.Set( PT_SLOW_MOTION, pSlow( c.slow ) );
			if( !rig.Render( 3 ) )
				return 1;
			press( rig, PT_POP );
			const double um = hThickness( pThickness( 1.5 ) ), D = hDrainage( pDrainage( 4.0 ) );
			const int64_t agePop = ageAt( 2, 60.0 );
			const double front   = hp::front( D * static_cast< double >( agePop ) * 1e-6, hp::t0( um * 1e-6 ) );
			const double nx = 0.15 + 0.7 * hash01( 1, 100 ), ny = 1.0 - front < 0.04 ? 0.98 : 0.5 * ( 1.0 + front );
			std::string lines;
			int outside = 0, inside = 0;
			for( const int j : { 4, 9 } )
			{
				const int k = 3 + j;
				if( !rig.Render( k + 1 - rig.frame ) )
					return 1;
				const double t    = static_cast< double >( ageAt( k, 60.0 ) - agePop ) * 1e-6;
				const double r    = hp::speed( c.h ) * t / hSlow( pSlow( c.slow ) );
				const double band = r * 64.0 * kFloat + std::hypot( W, H ) * 64.0 * kFloat;
				const Floats out  = rig.Output();
				int open = 0, predicted = 0;
				for( int y = 0; y < h; ++y )
					for( int x = 0; x < w; ++x )
					{
						const size_t p  = ( static_cast< size_t >( y ) * w + x ) * 4;
						const bool hole = out[ p ] == 0.0f && out[ p + 1 ] == 0.0f && out[ p + 2 ] == 0.0f;
						const double d  = std::hypot( ( ( x + 0.5 ) / w - nx ) * W, ( ( y + 0.5 ) / h - ny ) * H );
						open += hole;
						predicted += d <= r;
						if( hole != ( d <= r ) )
							( std::fabs( d - r ) <= band ? inside : outside )++;
					}
				lines += fmt( " %.3f s: r %.3f mm, %d open (%d predicted);", t, r * 1e3, open, predicted );
			}
			Check( outside == 0, fmt( "%4dx%-4d %4.0f nm (u = %.2f m/s), Slow Motion %.0f, from (%.4f, %.4f):%s %d pixels off the circle beyond float "
			                          "(r x 64 x 2^-24), %d within it",
			                          w, h, c.h, hp::speed( c.h ), c.slow, nx, ny, lines.c_str(), outside, inside ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --refill: New Film, and the cycle: burst, empty for Refill seconds, again
//===========================================================================
int runRefill( const Perturb& perturb )
{
	std::printf( "\n=== refill: New Film mid-drain gives the picture of a fresh film one frame old (bit-exact); left alone, the film bursts when\n"
	             "    the front has come 10%% of the way down, shows nothing (empty) for the burst's span + Refill, then a fresh film whose picture\n"
	             "    is bit for bit a fresh instance's at the same age -- every instant from the harness's own arithmetic\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const Floats card = lookCard( w, h );
		// New Film.
		Rig a, fresh;
		if( !startRig( a, w, h, perturb, &card, 10.0 ) || !startRig( fresh, w, h, perturb, &card, 10.0 ) )
			return 1;
		if( !a.Render( 50 ) )
			return 1;
		press( a, PT_NEW_FILM );
		if( !a.Render( 1 ) || !fresh.Render( 2 ) )
			return 1;
		const size_t newFilm = differing( a.Output(), fresh.Output() );

		// The cycle.
		Rig b;
		if( !startRig( b, w, h, perturb, &card, 10.0 ) )
			return 1;
		const float pB = 0.1f, pR = pRefill( 0.5 ), pS = pSlow( 1.0 );
		auto cycling = [ & ]( Rig& r ) {
			r.Set( PT_BURST, pB );
			r.Set( PT_REFILL, pR );
			r.Set( PT_SLOW_MOTION, pS );
			r.Set( PT_DRAINAGE, pDrainage( 10.0 ) );
		};
		cycling( b );
		const double um = hThickness( pThickness( 1.5 ) ), D = hDrainage( pDrainage( 10.0 ) );
		const double reach   = static_cast< double >( pB );
		const double tauB    = hp::t0( um * 1e-6 ) + hp::T( 30.0 * 1e-9 ) * hp::finv( 1.0 - reach );
		const int64_t tB     = static_cast< int64_t >( std::ceil( tauB / D * 1e6 ) );
		const int64_t spanB  = static_cast< int64_t >( std::ceil( std::sqrt( 1.0 + ( static_cast< double >( w ) / h ) * ( static_cast< double >( w ) / h ) ) * 0.025
		                                                          / hp::speed( 4000.0 ) * hSlow( pS ) * 1e6 ) );
		const int64_t spanE  = std::llround( hRefill( pR ) * 1e6 );
		const int64_t tR     = tB + spanB + spanE;
		int emptyFrames = 0, emptyWrong = 0, kR = -1;
		for( int k = 0; kR < 0 && k < 2000; ++k )
		{
			if( !b.Render( 1 ) )
				return 1;
			const int64_t at = ageAt( k, 10.0 );
			if( at >= tR )
			{
				kR = k;
				break;
			}
			if( at >= tB + spanB )
			{
				++emptyFrames;
				const Floats out = b.Output();
				for( size_t p = 0; p < out.size(); p += 4 )
					if( out[ p ] != 0.0f || out[ p + 1 ] != 0.0f || out[ p + 2 ] != 0.0f )
					{
						++emptyWrong;
						break;
					}
			}
		}
		size_t refilled = std::numeric_limits< size_t >::max();
		if( kR >= 0 )
		{
			Rig f;
			f.timeScale = 0.0;
			f.timeOffset = static_cast< double >( tR ) / 1e6;
			if( !startRig( f, w, h, perturb, &card, 10.0 ) )
				return 1;
			cycling( f );
			if( !f.Render( 1 ) )
				return 1;
			f.timeOffset = static_cast< double >( kR ) / 10.0;
			if( !f.Render( 1 ) )
				return 1;
			refilled = differing( b.Output(), f.Output() );
		}
		Check( newFilm == 0, fmt( "%4dx%-4d New Film at 5 s against a fresh instance's second frame: %zu values differ (bit-exact)", w, h, newFilm ) );
		Check( kR >= 0 && emptyFrames > 0 && emptyWrong == 0 && refilled == 0,
		       fmt( "%4dx%-4d burst at %.6f s of age (the front 10%% down), %.6f s of burst, %.6f s empty: %d frames empty, %d of them showing "
		            "anything; the new film at frame %d (%.6f s) against a fresh instance at its age (%lld us): %zu values differ (bit-exact)",
		            w, h, tB * 1e-6, spanB * 1e-6, spanE * 1e-6, emptyFrames, emptyWrong, kR, tR * 1e-6,
		            static_cast< long long >( kR >= 0 ? ageAt( kR, 10.0 ) - tR : 0 ), refilled ) );
	}
	return Verdict();
}

//===========================================================================
// --alpha
//===========================================================================
int runAlpha( const Perturb& perturb )
{
	std::printf( "\n=== alpha: output alpha = mix( a, 1, Mix ) (the film is an opaque reflection); the clip is reflected premultiplied, so white\n"
	             "    under alpha 0 changes nothing; Mix 0 hands the clip through; Mix 0.5 mixes premultiplied; a 500 nm film, every pixel's colour\n"
	             "    against Exposure x tint x the clip's linear light x alpha\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const Floats look = lookCard( w, h );
		Floats white = look, black = look;
		for( int y = 0; y < h; ++y )
			for( int x = 0; x < w; ++x )
			{
				const size_t p = ( static_cast< size_t >( y ) * w + x ) * 4;
				if( x < w / 2 )
				{
					white[ p ] = white[ p + 1 ] = white[ p + 2 ] = 1.0f;
					black[ p ] = black[ p + 1 ] = black[ p + 2 ] = 0.0f;
					white[ p + 3 ] = black[ p + 3 ] = 0.0f;
				}
				else
					white[ p + 3 ] = black[ p + 3 ] = std::round( 255.0f * ( 2.0f * x / w - 1.0f ) ) / 255.0f;
			}
		Rig rw, rb;
		if( !startRig( rw, w, h, perturb, &white ) || !startRig( rb, w, h, perturb, &black ) )
			return 1;
		for( Rig* r : { &rw, &rb } )
			r->plugin.HooksForTest().forceNm = 500.0;
		const double E = hExposure( pExposure( 6.0 ) );
		double worst[ 2 ]      = {};
		size_t shows[ 2 ]      = {};
		const float mixes[ 2 ] = { 1.0f, 0.5f };
		Floats mixed[ 2 ];
		for( int k = 0; k < 2; ++k )
		{
			for( Rig* r : { &rw, &rb } )
			{
				r->Set( PT_MIX, mixes[ k ] );
				if( !r->Render( 1 ) )
					return 1;
			}
			const Floats ow = rw.Output(), ob = rb.Output();
			mixed[ k ]      = ow;
			for( size_t p = 0; p < ow.size() / 4; ++p )
			{
				const double a = white[ 4 * p + 3 ];
				worst[ k ]     = worse( worst[ k ], std::fabs( ow[ 4 * p + 3 ] - ( a + ( 1.0 - a ) * mixes[ k ] ) ) );
			}
			shows[ k ] = differing( ow, ob );
		}
		// Mix 1's colour, every pixel.
		const Expect unit = expectFilm( 500.0, 0.0, E, 1.0 );
		double colour     = 0.0;
		for( size_t p = 0; p < white.size() / 4; ++p )
		{
			const double a = white[ 4 * p + 3 ];
			for( int c = 0; c < 3; ++c )
			{
				const double clip = linearOf( white[ 4 * p + c ] ) * a;
				const double want = unit.want[ c ] * clip, tol = unit.tol[ c ] * clip + 1e-9;
				colour            = worse( colour, std::fabs( linearOf( mixed[ 0 ][ 4 * p + c ] ) - want ) / tol );
			}
		}
		rw.Set( PT_MIX, 0.0f );
		if( !rw.Render( 1 ) )
			return 1;
		const size_t through = differing( rw.Output(), white );
		double worstMix      = 0.0;
		for( size_t p = 0; p < white.size() / 4; ++p )
		{
			const double a = white[ 4 * p + 3 ];
			for( int c = 0; c < 3; ++c )
			{
				const double want = ( white[ 4 * p + c ] * a * 0.5 + mixed[ 0 ][ 4 * p + c ] * 0.5 ) / ( a * 0.5 + 0.5 );
				worstMix          = worse( worstMix, std::fabs( mixed[ 1 ][ 4 * p + c ] - want ) );
			}
		}
		Check( worst[ 0 ] <= 1e-6 && worst[ 1 ] <= 1e-6 && shows[ 0 ] == 0 && shows[ 1 ] == 0 && through == 0 && worstMix <= 1e-6 && colour <= 1.0,
		       fmt( "%4dx%-4d alpha off mix( a, 1, Mix ) by %.2g at Mix 1 and %.2g at Mix 0.5 (tolerance 1e-6, a few float operations on numbers <= 1); "
		            "white rather than black under alpha 0 changes %zu values at Mix 1 and %zu at Mix 0.5 (bit-exact); Mix 1's colour at worst %.3f of "
		            "its tolerance (--colour's, x the clip's linear light x alpha); Mix 0: %zu values differ from the clip (bit-exact); Mix 0.5's "
		            "colour off the premultiplied mix by %.2g (tolerance 1e-6)",
		            w, h, worst[ 0 ], worst[ 1 ], shows[ 0 ], shows[ 1 ], colour, through, worstMix ) );
	}
	return Verdict();
}

//===========================================================================
// --glstate
//===========================================================================
int runGLState( const Perturb& perturb )
{
	std::printf( "\n=== glstate: the host's GL state comes back as it was handed over (stirring on, so the advection passes run too)\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		stirred( rig );
		GLuint vao = 0, buffer = 0, unpack = 0;
		glGenVertexArrays( 1, &vao );
		glGenBuffers( 1, &buffer );
		glGenBuffers( 1, &unpack );
		glBindBuffer( GL_PIXEL_UNPACK_BUFFER, unpack );
		glBufferData( GL_PIXEL_UNPACK_BUFFER, 64, nullptr, GL_STREAM_DRAW );
		glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
		int wrong = 0;
		for( int frame = 0; frame < 3; ++frame )
		{
			rig.plugin.SetTime( frame / 60.0 );
			glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
			glViewport( 3, 5, raster.w - 7, raster.h - 9 );
			glEnable( GL_BLEND );
			glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO );
			glClearColor( 0.1f, 0.2f, 0.3f, 0.4f );
			glBindVertexArray( vao );
			glBindBuffer( GL_ARRAY_BUFFER, buffer );
			glBindBuffer( GL_PIXEL_UNPACK_BUFFER, unpack );
			glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
			glPixelStorei( GL_UNPACK_ROW_LENGTH, 7 );
			glActiveTexture( GL_TEXTURE3 );
			if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
				return 1;
			GLint vp[ 4 ], src = 0, dst = 0, srcA = 0, dstA = 0, va = 0, ab = 0, at = 0, ub = 0, ua = 0, url = 0, fb = 0, prog = 0;
			GLfloat cc[ 4 ];
			glGetIntegerv( GL_VIEWPORT, vp );
			glGetIntegerv( GL_BLEND_SRC_RGB, &src );
			glGetIntegerv( GL_BLEND_DST_RGB, &dst );
			glGetIntegerv( GL_BLEND_SRC_ALPHA, &srcA );
			glGetIntegerv( GL_BLEND_DST_ALPHA, &dstA );
			glGetFloatv( GL_COLOR_CLEAR_VALUE, cc );
			glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &va );
			glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &ab );
			glGetIntegerv( GL_ACTIVE_TEXTURE, &at );
			glGetIntegerv( GL_PIXEL_UNPACK_BUFFER_BINDING, &ub );
			glGetIntegerv( GL_UNPACK_ALIGNMENT, &ua );
			glGetIntegerv( GL_UNPACK_ROW_LENGTH, &url );
			glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fb );
			glGetIntegerv( GL_CURRENT_PROGRAM, &prog );
			wrong += vp[ 0 ] != 3 || vp[ 1 ] != 5 || vp[ 2 ] != raster.w - 7 || vp[ 3 ] != raster.h - 9;
			wrong += !glIsEnabled( GL_BLEND ) || src != GL_SRC_ALPHA || dst != GL_ONE || srcA != GL_ONE || dstA != GL_ZERO;
			wrong += cc[ 0 ] != 0.1f || cc[ 1 ] != 0.2f || cc[ 2 ] != 0.3f || cc[ 3 ] != 0.4f;
			wrong += va != static_cast< GLint >( vao ) || ab != static_cast< GLint >( buffer ) || at != GL_TEXTURE3;
			wrong += ub != static_cast< GLint >( unpack ) || ua != 1 || url != 7;
			wrong += fb != static_cast< GLint >( rig.outputFBO ) || prog != 0;
			wrong += glGetError() != GL_NO_ERROR;
			glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
			glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
			glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
		}
		const int64_t steps = rig.plugin.StepsRun();
		glBindVertexArray( 0 );
		glDisable( GL_BLEND );
		glActiveTexture( GL_TEXTURE0 );
		glDeleteVertexArrays( 1, &vao );
		glDeleteBuffers( 1, &buffer );
		glDeleteBuffers( 1, &unpack );
		Check( wrong == 0 && steps > 0,
		       fmt( "%4dx%-4d three frames (%lld advection steps among them) with a host viewport, blend, clear colour, vertex array, buffer, texture "
		            "unit and pixel-unpack buffer and store of its own: %d things not put back (the host's framebuffer bound, no program), GL errors included",
		            raster.w, raster.h, static_cast< long long >( steps ), wrong ) );
	}
	return Verdict();
}

//===========================================================================
// --robust
//===========================================================================
int runRobust( const Perturb& perturb )
{
	std::printf( "\n=== robust: a clip out of range (-1, 2, 1e30, +-inf and NaN in a float texture, alpha too) is the clip tamed (NaN 0, then\n"
	             "    clamped), bit for bit at Mix 1, through 3 s of stirred film; every control at its ends, for 3 s at 10 fps, gives finite output\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		Floats wild = lookCard( w, h ), tame;
		const float inf = std::numeric_limits< float >::infinity(), nan = std::numeric_limits< float >::quiet_NaN();
		const float values[] = { -1.0f, 2.0f, 1e30f, inf, -inf, nan, 0.5f, 0.0f, 1.0f };
		for( size_t p = 0; p < wild.size(); p += 4 )
		{
			const uint32_t k = static_cast< uint32_t >( p / 4 );
			if( hash01( k, 3 ) < 0.3 )
				for( int c = 0; c < 4; ++c )
					wild[ p + c ] = values[ pcg( k * 4 + static_cast< uint32_t >( c ) ) % 9 ];
		}
		tame = wild;
		for( float& v : tame )
			v = v != v ? 0.0f : std::clamp( v, 0.0f, 1.0f );
		Rig a, b;
		if( !startRig( a, w, h, perturb, &wild, 10.0 ) || !startRig( b, w, h, perturb, &tame, 10.0 ) )
			return 1;
		for( Rig* r : { &a, &b } )
		{
			stirred( *r );
			r->Set( PT_DRAINAGE, pDrainage( 4.0 ) );
		}
		if( !a.Render( 31 ) || !b.Render( 31 ) )
			return 1;
		const size_t differ = differing( a.Output(), b.Output() );
		int nonFinite = 0, tried = 0;
		bool passedThrough = false;
		for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		{
			const ControlInfo& info = InfoOf( id );
			for( const float end : { 0.0f, info.kind == Kind::Option ? static_cast< float >( info.optionCount - 1 ) : 1.0f } )
			{
				Rig r;
				if( !startRig( r, w, h, perturb, &wild, 10.0 ) )
					return 1;
				stirred( r );
				r.Set( PT_BURST, 0.5f );
				r.Set( id, end );
				if( !r.Render( 31 ) )
					return 1;
				const Floats out = r.Output();
				//Mix 0 hands the clip through untouched, NaN and all.
				if( id == PT_MIX && end == 0.0f )
				{
					passedThrough = std::memcmp( out.data(), wild.data(), wild.size() * sizeof( float ) ) == 0;
					continue;
				}
				for( const float v : out )
					if( !std::isfinite( v ) )
					{
						++nonFinite;
						break;
					}
				++tried;
			}
		}
		Check( differ == 0 && nonFinite == 0 && passedThrough,
		       fmt( "%4dx%-4d the wild clip against the tamed one after 3 s: %zu values differ (bit-exact); %d renders with one control at an end: "
		            "%d with any non-finite value; Mix 0 hands the wild clip through byte for byte: %s",
		            w, h, differ, tried, nonFinite, passedThrough ? "yes" : "NO" ) );
	}
	return Verdict();
}

//===========================================================================
// The registry.
//===========================================================================
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "model", runModel, true },     { "colour", runColour, false }, { "black", runBlack, false },     { "drainage", runDrainage, false },
		{ "rate", runRate, false },      { "clock", runClock, false },   { "swirl", runSwirl, false },     { "resize", runResize, false },
		{ "burst", runBurst, false },    { "refill", runRefill, false }, { "alpha", runAlpha, false },     { "glstate", runGLState, false },
		{ "robust", runRobust, false },
	};
	return list;
}

bool isOffline( const std::string& flag )
{
	for( const CheckEntry& c : checks() )
		if( flag == c.flag )
			return c.offline;
	return false;
}

//===========================================================================
// --negative
//===========================================================================
int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	add( "model", runModel, "no half-wave phase change at the second face", []( Perturb& p ) { p.opticsModel = film::kNoHalfWave; } );
	add( "model", runModel, "no dispersion: n fixed at n(550 nm)", []( Perturb& p ) { p.opticsModel = film::kNoDispersion; } );
	add( "model", runModel, "s polarisation only", []( Perturb& p ) { p.opticsModel = film::kSOnly; } );
	add( "model", runModel, "the drainage exponent n = 1", []( Perturb& p ) { p.drainageModel = film::kExponentOne; } );
	add( "colour", runColour, "no half-wave phase change at the second face", []( Perturb& p ) { p.opticsModel = film::kNoHalfWave; } );
	add( "colour", runColour, "no dispersion: n fixed at n(550 nm)", []( Perturb& p ) { p.opticsModel = film::kNoDispersion; } );
	add( "colour", runColour, "s polarisation only", []( Perturb& p ) { p.opticsModel = film::kSOnly; } );
	add( "black", runBlack, "the law followed below h_cr: no black film", []( Perturb& p ) { p.noTransition = true; } );
	add( "drainage", runDrainage, "the drainage exponent n = 1", []( Perturb& p ) { p.drainageModel = film::kExponentOne; } );
	add( "drainage", runDrainage, "the birth offset t0 ignored", []( Perturb& p ) { p.drainageModel = film::kNoBirthOffset; } );
	add( "drainage", runDrainage, "a fixed 1/60 s a frame, whatever the host's clock (run at 10 fps)", []( Perturb& p ) { p.clockModel = 1; } );
	add( "rate", runRate, "one advection step a frame, of the frame's interval", []( Perturb& p ) { p.stepModel = 1; } );
	add( "clock", runClock, "the host's time read in float", []( Perturb& p ) { p.clockModel = 2; } );
	add( "swirl", runSwirl, "the flow's sign flipped", []( Perturb& p ) { p.flowModel = 1; } );
	add( "swirl", runSwirl, "Swirl ignored", []( Perturb& p ) { p.flowModel = 2; } );
	add( "resize", runResize, "a new raster restarts the film", []( Perturb& p ) { p.clearOnResize = true; } );
	add( "burst", runBurst, "1/u proportional to h (equal to Taylor-Culick at 300 nm only)", []( Perturb& p ) { p.speedModel = 1; } );
	add( "refill", runRefill, "New Film ignored", []( Perturb& p ) { p.newFilmIgnored = true; } );
	add( "alpha", runAlpha, "the clip's alpha passed through", []( Perturb& p ) { p.alphaModel = 1; } );
	add( "alpha", runAlpha, "the clip's straight colour reflected", []( Perturb& p ) { p.premultiply = false; } );
	add( "glstate", runGLState, "the host's GL state not put back", []( Perturb& p ) { p.leakGLState = true; } );
	add( "robust", runRobust, "the clip's NaN and range not tamed", []( Perturb& p ) { p.clampOn = false; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}

int runOffline()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		if( check.offline )
			failed |= check.run( Perturb {} );
	failed |= runNegative( true );
	std::printf( "\n  offline: the checks that need no GL context. The picture checks were NOT run --\n"
	             "  tools/verify.sh runs them against a real driver, at 1280x720 and 320x180, and again on the software renderer.\n"
	             "\n  %s\n",
	             failed == 0 ? "PASS" : "FAIL" );
	return failed == 0 ? 0 : 1;
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED.
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
		int warm, timed;
		bool stirring;
	};
	const Size sizes[] = {
		{ 1280, 720, "720p", 20, 60, false }, { 1920, 1080, "1080p", 20, 60, false }, { 1280, 720, "720p", 20, 60, true },
		{ 1920, 1080, "1080p", 20, 60, true }, { 3840, 2160, "4K", 5, 10, true },
	};
	std::printf( "\n=== bench: GPU time per frame (GL_TIME_ELAPSED) on the look card at 60 fps: the film still (Swirl 0), then stirred (the\n"
	             "    default Swirl, 3 mm/s: 12 advection steps a frame)\n" );
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Size& size : sizes )
	{
		Rig rig;
		const Floats card = lookCard( size.w, size.h );
		if( !rig.Init( size.w, size.h, &card ) )
			return 1;
		if( !size.stirring )
			rig.Set( PT_SWIRL, 0.0f );
		if( !rig.Render( size.warm ) )
			return 1;
		std::vector< double > gpu;
		const int64_t stepsBefore = rig.plugin.StepsRun();
		for( int f = 0; f < size.timed; ++f )
		{
			glBeginQuery( GL_TIME_ELAPSED, query );
			if( !rig.Render( 1 ) )
				return 1;
			glEndQuery( GL_TIME_ELAPSED );
			GLuint64 ns = 0;
			glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
			gpu.push_back( static_cast< double >( ns ) * 1e-6 );
		}
		std::sort( gpu.begin(), gpu.end() );
		std::printf( "  %-6s %-8s %5.1f steps a frame  GPU median %6.2f ms, worst %6.2f (%5.1f%% of a 60 fps frame)\n", size.name,
		             size.stirring ? "stirred" : "still", static_cast< double >( rig.plugin.StepsRun() - stepsBefore ) / size.timed, gpu[ gpu.size() / 2 ],
		             gpu.back(), 100.0 * gpu[ gpu.size() / 2 ] / ( 1000.0 / 60.0 ) );
	}
	glDeleteQueries( 1, &query );
	return 0;
}
