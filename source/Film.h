#pragma once

#include <cstdint>
#include <vector>

/**
    The film on the CPU, in double: its optics (water's index, the Airy
    reflectance, the colour table), its drainage law, the black film, the
    Taylor-Culick burst, the stirring's coefficients, and the clock that says
    which of draining, bursting or empty it is. SPEC-bubble.md, The mechanism.

    Nothing here touches GL; the harness calls it offline.
*/
namespace bubble::film
{
constexpr double kPi = 3.14159265358979323846;

//---------------------------------------------------------------------------
// The physical film (SPEC-bubble.md).
//---------------------------------------------------------------------------
constexpr double kFilmHeight = 0.025;///< m: the frame's height (Monier et al.'s H)
/// T(h) = kappa h^-n, Monier et al.'s example film (Fig. 3).
constexpr double kExponentN = 0.72;
constexpr double kKappa     = 2.4e-4;///< m^0.72 s
/// The master curve f(u) = 1 - (1 - exp(-(u/a)^b))^c, fitted to their Fig. 3b
/// with f(1) = 0.8 and f'(1) = -0.44.
constexpr double kFitA = 0.13145, kFitB = 0.48916, kFitC = 23.08939;
constexpr double kCriticalNm    = 30.0;  ///< h_cr: below it, black film
constexpr double kCommonBlackNm = 12.0;  ///< Reinold & Ruecker (via Ziapkoff et al.)
constexpr double kNewtonBlackNm = 4.0;   ///< Pugh
constexpr double kMaxNm         = 4000.0;///< the thickness cap, the colour table's end
constexpr double kSurfaceTension = 0.035;///< N/m, Monier et al.'s solutions
constexpr double kDensity        = 998.20715;///< kg/m^3, water at 20 C, 0.101325 MPa (NIST WebBook)
constexpr double kTemperature    = 20.0; ///< C

//---------------------------------------------------------------------------
// Optics.
//---------------------------------------------------------------------------
/// The harness's wrong models of the optics; 0 is the plugin.
enum OpticsModel
{
	kOptics          = 0,
	kNoHalfWave      = 1,///< r12 = +r01: no half-wave phase change
	kNoDispersion    = 2,///< n(lambda) fixed at n(550 nm)
	kSOnly           = 3,///< s polarisation only
	kOpticsModels    = 4
};

/// IAPWS R9-97, eq. (1): water's refractive index at lambda nm, T C, rho kg/m^3.
double WaterIndex( double lambdaNm, double celsius = kTemperature, double density = kDensity );
/// n(550 nm) at 20 C: the colour table's reference for its thickness axis.
double ReferenceIndex();
/// cos theta_t at the reference index, theta in radians.
double CosRefracted( double theta );
/// The film's reflectance, air | water (h nm) | air, unpolarised (the mean of
/// s and p), at lambda nm and incidence theta radians.
double Reflectance( double hNm, double theta, double lambdaNm, int model = kOptics );

/// The colour table (SPEC-bubble.md, The lookup texture): RGBA32F,
/// kTintColumns x kTintRows, column i at optical thickness h cos theta_t,ref =
/// i kTintStepNm, row j at theta = j kTintAngleStep degrees.
constexpr int kTintColumns       = 2001;
constexpr double kTintStepNm     = 2.0;
constexpr int kTintRows          = 121;
constexpr double kTintAngleStep  = 0.5;///< degrees
struct TintTable
{
	std::vector< float > rgba;
	double white[ 3 ] = {};///< M times the D65 white (Y = 1): what a mirror would read before the correction
	double seconds = 0.0;  ///< the build's wall time
};
/// Built once per process per model, on first use (threads over rows).
const TintTable& Tints( int model = kOptics );
/// The direct sum for one film, linear sRGB, white-corrected: what the table holds at its nodes.
void TintDirect( double hNm, double theta, int model, double rgb[ 3 ] );

//---------------------------------------------------------------------------
// Drainage (Monier et al. 2024).
//---------------------------------------------------------------------------
/// The harness's wrong models of the drainage; 0 is the plugin.
enum DrainageModel
{
	kDrainage       = 0,
	kExponentOne    = 1,///< n = 1
	kNoBirthOffset  = 2 ///< t0 ignored
};
double MasterCurve( double u );///< f
double MasterInverse( double y );///< f^-1, y in [0, 1]
double ExponentN( int model );
/// T(h) = kappa h^-n, h in metres, seconds of drainage time.
double TimeScale( double hMetres, int model = kDrainage );
/// t0 for a film hMid metres thick at mid-height at birth (negative).
double BirthOffset( double hMidMetres, int model = kDrainage );
/// A(tau) = (kappa / (tau - t0))^m, metres.
double Amplitude( double tau, double t0, int model = kDrainage );
/// G(y) = [f^-1(y)]^m: h = A G.
double Shape( double y, int model = kDrainage );
/// The height (0 bottom, 1 top) of the black front at drainage time tau.
double FrontHeight( double tau, double t0, int model = kDrainage );
/// The drainage time at which the black front reaches `reach` of the way down
/// from the top; +inf for reach >= 1 (never).
double BurstTime( double t0, double reach, int model = kDrainage );

/// G for the shader: two rows of kShapeSize, row 0 at y = w^4 and row 1 at
/// y = 1 - w^4, w = i / (kShapeSize - 1) * 0.5^(1/4). Capped at kShapeCap.
constexpr int kShapeSize    = 2048;
constexpr double kShapeCap  = 1e20;
double ShapeTableW( int i );
std::vector< float > ShapeTable( int model = kDrainage );

//---------------------------------------------------------------------------
// The burst.
//---------------------------------------------------------------------------
/// Taylor-Culick, sqrt( 2 gamma / ( rho h ) ), m/s.
double RetractionSpeed( double hNm );
/// 1/u = sqrt( h_nm ) * this, s/m.
double InverseSpeedPerRootNm();
/// The burst is over (every pixel open) by this many seconds of burst time:
/// the frame's diagonal at the slowest speed, u( kMaxNm ).
double BurstDuration( double aspect );

//---------------------------------------------------------------------------
// The stirring: six box modes of the frame (SPEC-bubble.md, The stirring).
//---------------------------------------------------------------------------
constexpr int kModes    = 6;
constexpr int kStepRate = 720;///< advection steps a second of film age
uint32_t Pcg( uint32_t v );
/// The generation's hash in [0, 1) for slot `index`.
double Hash01( uint32_t generation, uint32_t index );
struct Flow
{
	int p[ kModes ] = {}, q[ kModes ] = {};
	double weight[ kModes ] = {}, frequency[ kModes ] = {}, phase[ kModes ] = {};
	double bound = 0.0;///< sum weight pi sqrt( (p/W)^2 + (q/H)^2 ), 1/m
};
Flow FlowFor( uint32_t generation, double aspect );
/// c_k at film age t seconds, in the frame's normalised units (1/s):
/// dX/dt = sum c q sin(p pi X) cos(q pi Y), dY/dt = -sum c p cos(p pi X) sin(q pi Y).
void FlowCoefficients( const Flow& flow, double swirlMetresPerSecond, double aspect, double t, double c[ kModes ] );

//---------------------------------------------------------------------------
// The film's clock: draining, bursting, empty (SPEC-bubble.md, The burst).
//---------------------------------------------------------------------------
enum class Phase
{
	Draining,
	Bursting,
	Empty
};
struct Settings
{
	double thicknessUm   = 1.5;
	double drainage      = 4.0;
	double reach         = 0.6;
	double slowMotion    = 100.0;
	double refillSeconds = 1.0;
	double aspect        = 16.0 / 9.0;
	int drainageModel    = kDrainage;
};
class Clock
{
public:
	/// A fresh film: the generation increments; age, tau and the steps restart.
	void Restart();
	/// Run `micros` of host time. `pop` bursts a draining film now; `newFilm`
	/// restarts from any state before the time runs.
	void Advance( int64_t micros, const Settings& settings, bool pop, bool newFilm );

	Phase phase         = Phase::Draining;
	uint32_t generation = 0;
	int64_t age         = 0;  ///< microseconds of draining, frozen while bursting or empty
	double tau          = 0.0;///< drainage time, s (frozen at the burst)
	double t0           = 0.0;
	int64_t burstMicros = 0;  ///< host time since the hole nucleated
	int64_t emptyMicros = 0;
	double nucleation[ 2 ] = {};///< X, Y of the hole's start, 0..1
	int64_t StepsDue() const
	{
		return ( age * kStepRate + 500000 ) / 1000000;
	}

private:
	double tauAt( int64_t at ) const;
	void burst( double front );

	double tauBase      = 0.0;
	int64_t ageBase     = 0;
	double drainageBase = -1.0;
};

} // namespace bubble::film
