#pragma once

#include "StoatworksAboutLinks.h"

/**
    The host's parameters, and what they mean in physical units.

    Every ranged STANDARD parameter the host sees is 0..1, because
    `SetParamInfo` clamps an `FF_TYPE_STANDARD` default into 0..1 before
    `SetParamRange` could widen it. The conversions live in Controls.cpp, one
    function per control, each with its inverse where a default or the harness
    needs it. Option parameters hold the element index (an option reads back
    0..1 whatever its count: map by index). Events are latched on their rising
    edge and consumed by the next frame.

    Units: micrometres for the film's thickness, millimetres a second for the
    stirring, seconds for the refill, degrees for the angle of incidence; two
    multipliers (Drainage, the law's clock; Slow Motion, the burst's) mapped
    logarithmically; Exposure is linear light (a film reflecting tint 1/Exposure
    shows the clip at full code).
*/
namespace bubble
{

/// Every control, by what it is. The host sees them in this order, with the
/// About block last.
enum ParamId : unsigned int
{
	// -- Film --------------------------------------------------------------------
	PT_THICKNESS = 0,
	PT_DRAINAGE,
	PT_SWIRL,
	PT_BLACK_FILM,

	// -- Burst -------------------------------------------------------------------
	PT_BURST,
	PT_SLOW_MOTION,
	PT_REFILL,
	PT_POP,
	PT_NEW_FILM,

	// -- Light -------------------------------------------------------------------
	PT_ANGLE,
	PT_EXPOSURE,

	// -- Look --------------------------------------------------------------------
	PT_MIX,

	// -- The Stoatworks About block: a text line, then one button per link.
	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option,
	Event
};

/// One control as the host is told about it.
struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, or the element index
	const char* const* options;
	int optionCount;
};

/// The table, indexed by ParamId, for every id below PT_ABOUT_TEXT.
const ControlInfo& InfoOf( unsigned int id );

/// An option's index from the value the host stored.
int OptionIndex( float value, int count );

enum BlackFilm
{
	kCommonBlack = 0,///< 12 nm: Reinold & Ruecker's soap films
	kNewtonBlack = 1 ///< 4 nm: Pugh
};

//---------------------------------------------------------------------------
// Conversions, 0..1 to physical and back.
//---------------------------------------------------------------------------
constexpr double kThicknessLow = 0.2, kThicknessHigh = 4.0;///< micrometres
constexpr double kDrainageLow = 0.1, kDrainageHigh = 10.0; ///< x, logarithmic
constexpr double kSwirlHigh      = 20.0;                   ///< mm/s
constexpr double kSlowLow = 1.0, kSlowHigh = 1000.0;       ///< x, logarithmic
constexpr double kRefillHigh     = 10.0;                   ///< s
constexpr double kAngleHigh      = 60.0;                   ///< degrees
constexpr double kExposureHigh   = 50.0;

double ThicknessFromParam( float v );///< micrometres at mid-height at birth
float ParamFromThickness( double um );
double DrainageFromParam( float v );
float ParamFromDrainage( double times );
double SwirlFromParam( float v );///< mm/s, the bound on the stirring's speed
float ParamFromSwirl( double mmPerSecond );
int BlackFilmFromParam( float v );
double BurstFromParam( float v );///< the black front's reach from the top, 0..1; 1 is never
float ParamFromBurst( double reach );
double SlowMotionFromParam( float v );
float ParamFromSlowMotion( double times );
double RefillFromParam( float v );///< s
float ParamFromRefill( double seconds );
double AngleFromParam( float v );///< degrees of incidence
float ParamFromAngle( double degrees );
double ExposureFromParam( float v );
float ParamFromExposure( double level );

/// Exposure's default, from Resolume's demo clips (tools/clips.py defaults).
extern const double kDefaultExposure;

} // namespace bubble
