#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace bubble
{
namespace
{
double clamp01( float v )
{
	//NaN reads as 0, never as a NaN thickness.
	return v == v ? std::clamp( static_cast< double >( v ), 0.0, 1.0 ) : 0.0;
}
double linear( float v, double low, double high )
{
	return low + ( high - low ) * clamp01( v );
}
float inverseLinear( double value, double low, double high )
{
	return static_cast< float >( std::clamp( ( value - low ) / ( high - low ), 0.0, 1.0 ) );
}
double logarithmic( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseLogarithmic( double value, double low, double high )
{
	return static_cast< float >( std::clamp( std::log( value / low ) / std::log( high / low ), 0.0, 1.0 ) );
}

const char* const kBlackNames[] = { "Common", "Newton" };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0 };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count };
}
ControlInfo event( unsigned int id, const char* name, const char* group )
{
	return { id, name, group, Kind::Event, 0.0f, nullptr, 0 };
}

//---------------------------------------------------------------------------
// The defaults (SPEC-bubble.md, Controls). A 1.5 um film at mid-height,
// draining at 4x the real 25 mm film's rate, stirred at up to 3 mm/s
// (chosen); common black film (Reinold & Ruecker's); bursting when the black
// film is 0.6 of the way down, shown at 100x slow motion, refilled after 1 s
// (chosen); facing the film; Exposure from the demo clips.
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	standard( PT_THICKNESS, "Thickness", "Film", ParamFromThickness( 1.5 ) ),
	standard( PT_DRAINAGE, "Drainage", "Film", ParamFromDrainage( 4.0 ) ),
	standard( PT_SWIRL, "Swirl", "Film", ParamFromSwirl( 3.0 ) ),
	option( PT_BLACK_FILM, "Black Film", "Film", 0.0f, kBlackNames, 2 ),
	standard( PT_BURST, "Burst", "Burst", ParamFromBurst( 0.6 ) ),
	standard( PT_SLOW_MOTION, "Slow Motion", "Burst", ParamFromSlowMotion( 100.0 ) ),
	standard( PT_REFILL, "Refill", "Burst", ParamFromRefill( 1.0 ) ),
	event( PT_POP, "Pop", "Burst" ),
	event( PT_NEW_FILM, "New Film", "Burst" ),
	standard( PT_ANGLE, "Angle", "Light", 0.0f ),
	standard( PT_EXPOSURE, "Exposure", "Light", ParamFromExposure( kDefaultExposure ) ),
	standard( PT_MIX, "Mix", "Look", 1.0f ),
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_TEXT, "every control needs a row in the table" );
} // namespace

// tools/clips.py defaults, 2026-11-17: over the 33 demo clips at Exposure 1, the
// median of each clip's 99th-percentile brightest output channel is 0.011594
// (the film reflects about 4% of the clip), so 1 / 0.011594.
const double kDefaultExposure = 86.2515;

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_TEXT - 1 ) ];
}

int OptionIndex( float value, int count )
{
	const double v = value == value ? static_cast< double >( value ) : 0.0;
	const long i   = std::lround( std::clamp( v, -1.0, static_cast< double >( count ) ) );
	return static_cast< int >( std::clamp< long >( i, 0, count - 1 ) );
}

double ThicknessFromParam( float v )
{
	return linear( v, kThicknessLow, kThicknessHigh );
}
float ParamFromThickness( double um )
{
	return inverseLinear( um, kThicknessLow, kThicknessHigh );
}
double DrainageFromParam( float v )
{
	return logarithmic( v, kDrainageLow, kDrainageHigh );
}
float ParamFromDrainage( double times )
{
	return inverseLogarithmic( times, kDrainageLow, kDrainageHigh );
}
double SwirlFromParam( float v )
{
	return linear( v, 0.0, kSwirlHigh );
}
float ParamFromSwirl( double mmPerSecond )
{
	return inverseLinear( mmPerSecond, 0.0, kSwirlHigh );
}
int BlackFilmFromParam( float v )
{
	return OptionIndex( v, 2 );
}
double BurstFromParam( float v )
{
	return clamp01( v );
}
float ParamFromBurst( double reach )
{
	return inverseLinear( reach, 0.0, 1.0 );
}
double SlowMotionFromParam( float v )
{
	return logarithmic( v, kSlowLow, kSlowHigh );
}
float ParamFromSlowMotion( double times )
{
	return inverseLogarithmic( times, kSlowLow, kSlowHigh );
}
double RefillFromParam( float v )
{
	return linear( v, 0.0, kRefillHigh );
}
float ParamFromRefill( double seconds )
{
	return inverseLinear( seconds, 0.0, kRefillHigh );
}
double AngleFromParam( float v )
{
	return linear( v, 0.0, kAngleHigh );
}
float ParamFromAngle( double degrees )
{
	return inverseLinear( degrees, 0.0, kAngleHigh );
}
double ExposureFromParam( float v )
{
	return linear( v, 0.0, kExposureHigh );
}
float ParamFromExposure( double level )
{
	return inverseLinear( level, 0.0, kExposureHigh );
}

} // namespace bubble
