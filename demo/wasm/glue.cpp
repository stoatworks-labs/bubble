/*
    The page's way in to the plugin: what an FFGL host does to a plugin
    instance, as C functions JavaScript can call. (pinhole's glue, with
    Bubble's names, its host clock and its film's read-outs.)

    Nothing here is the plugin, and nothing here does the plugin's work. The
    plugin is `source/Bubble.cpp` and everything it calls, compiled
    UNMODIFIED into the same WebAssembly module as this file (see
    demo/tools/build-wasm.sh for the list). This file only plays the host:

      - it constructs a `BubblePlugin`, as EffectPlugin.cpp's registration
        would, and calls InitGL / DeInitGL on it;
      - it reads the parameter declarations back through the FFGL SDK's own
        host-facing getters (CFFGLPluginManager::GetParamName and the rest,
        the SDK compiled unmodified too), so the page's panel is built from
        the plugin's constructor rather than from a copy of it;
      - it forwards the host's calls: SetFloatParameter, SetTime (the plugin
        declares SetTimeSupported, so a host hands it the time every frame),
        and ProcessOpenGL with ONE input texture (an effect), as
        tools/bbtest's Rig does.

    The rest are read-outs for the line under the picture: the plugin's own
    state through the accessors its harness reads (FilmClock(), StepsRun(),
    GridWidth(), GridHeight()), Film.h's functions of that state (the black
    front's height, the thickness at mid-height, the drainage time of the
    burst), and Controls.cpp's conversions for the panel. None of them changes
    what the plugin draws.

    One is a test seam and the page itself never calls it: bb_settle_clock
    is the plugin's SetClockScaleForTest, which demo/tools/crosscheck.py uses,
    as bbtest's Rig does, so that the plugin's clock is the host's time from
    the first frame rather than the wall clock while its vote on seconds or
    milliseconds runs. On the page the plugin votes, as it does in a host.

    GL calls go to the page's WebGL2 context through emscripten's GL library;
    demo/wasm/gl_shim.cpp replaces the five entry points that cannot be passed
    straight through.
*/
#include "Bubble.h"
#include "Controls.h"
#include "Diag.h"
#include "Film.h"

#include <emscripten/emscripten.h>

#include <sys/stat.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

using namespace bubble;

namespace
{
/// One plugin instance and the host-side structs it is handed.
struct Instance
{
	BubblePlugin plugin;
	bool initialised = false;

	FFGLTextureStruct input {};
	FFGLTextureStruct* inputs[ 1 ] = { &input };
	ProcessOpenGLStruct process {};

	std::string scratch;///< a string handed back to JS lives here until the next call
};

float asFloat( FFMixed mixed )
{
	float value = 0.0f;
	static_assert( sizeof( value ) == sizeof( mixed.UIntValue ), "FFMixed carries a float in its bits" );
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

const char* hold( Instance* instance, std::string text )
{
	instance->scratch = std::move( text );
	return instance->scratch.c_str();
}

/// The ParamId behind a host index, or PT_COUNT. Host indices ARE the
/// ParamIds here (Bubble.h), but the page asks by index and this keeps the
/// mapping in one place should that ever change.
unsigned int idOf( int index )
{
	return index >= 0 && index < static_cast< int >( PT_ABOUT_TEXT ) ? static_cast< unsigned int >( index ) : PT_COUNT;
}

constexpr double kNaN = std::numeric_limits< double >::quiet_NaN();
} // namespace

extern "C"
{
//---------------------------------------------------------------------------
// Lifetime.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE Instance* bb_new()
{
	return new Instance();
}

EMSCRIPTEN_KEEPALIVE void bb_delete( Instance* instance )
{
	if( instance == nullptr )
		return;
	if( instance->initialised )
		instance->plugin.DeInitGL();
	delete instance;
}

/// InitGL, with the viewport a host would pass. 1 on success.
EMSCRIPTEN_KEEPALIVE int bb_init_gl( Instance* instance, int width, int height )
{
	FFGLViewportStruct viewport {};
	viewport.width        = static_cast< GLuint >( width );
	viewport.height       = static_cast< GLuint >( height );
	instance->initialised = instance->plugin.InitGL( &viewport ) == FF_SUCCESS;
	return instance->initialised ? 1 : 0;
}

/// The test seam (above): the plugin's SetClockScaleForTest, before InitGL.
/// demo/tools/crosscheck.py only; the page lets the plugin vote.
EMSCRIPTEN_KEEPALIVE void bb_settle_clock( Instance* instance, double scale )
{
	instance->plugin.SetClockScaleForTest( scale );
}

//---------------------------------------------------------------------------
// The declarations, read as a host reads them.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int bb_param_count( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetNumParams() );
}

EMSCRIPTEN_KEEPALIVE const char* bb_param_name( Instance* instance, int index )
{
	const char* name = instance->plugin.GetParamName( static_cast< unsigned int >( index ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE int bb_param_type( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamType( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* bb_param_group( Instance* instance, int index )
{
	return hold( instance, instance->plugin.GetParamGroup( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a numeric parameter (FF_TYPE_STANDARD already
/// clamped into 0..1 by the SDK, as a host sees it).
EMSCRIPTEN_KEEPALIVE float bb_param_default( Instance* instance, int index )
{
	return asFloat( instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int bb_param_element_count( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetNumParamElements( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* bb_param_element_name( Instance* instance, int index, int element )
{
	const char* name = instance->plugin.GetParamElementName( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE float bb_param_element_value( Instance* instance, int index, int element )
{
	return asFloat( instance->plugin.GetParamElementDefault( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) ) );
}

EMSCRIPTEN_KEEPALIVE int bb_min_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMinInputs() );
}

EMSCRIPTEN_KEEPALIVE int bb_max_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMaxInputs() );
}

EMSCRIPTEN_KEEPALIVE int bb_time_supported( Instance* instance )
{
	return instance->plugin.IsTimeSupported() ? 1 : 0;
}

//---------------------------------------------------------------------------
// The host's calls.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int bb_set_float( Instance* instance, int index, float value )
{
	return instance->plugin.SetFloatParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE float bb_get_float( Instance* instance, int index )
{
	return instance->plugin.GetFloatParameter( static_cast< unsigned int >( index ) );
}

/// SetTime: the host's clock, in seconds (the page's clock; the plugin votes
/// on its unit against the wall clock, as it does in a host).
EMSCRIPTEN_KEEPALIVE int bb_set_time( Instance* instance, double seconds )
{
	return instance->plugin.SetTime( seconds ) == FF_SUCCESS ? 1 : 0;
}

/// One frame: ProcessOpenGL into whatever the page has bound (the canvas,
/// HostFBO 0, at the viewport the page set), handed one input of
/// clipWidth x clipHeight whose GL name is `clip` (a texture the page
/// registered with emscripten's GL tables). Width and HardwareWidth are the
/// same: a browser texture is not padded. 1 on success.
EMSCRIPTEN_KEEPALIVE int bb_process( Instance* instance, int clip, int clipWidth, int clipHeight )
{
	instance->input.Width = instance->input.HardwareWidth = static_cast< FFUInt32 >( clipWidth );
	instance->input.Height = instance->input.HardwareHeight = static_cast< FFUInt32 >( clipHeight );
	instance->input.Handle                                 = static_cast< GLuint >( clip );
	instance->process.numInputTextures                     = 1;
	instance->process.inputTextures                        = instance->inputs;
	instance->process.HostFBO                              = 0;
	return instance->plugin.ProcessOpenGL( &instance->process ) == FF_SUCCESS ? 1 : 0;
}

//---------------------------------------------------------------------------
// For the panel's read-outs: Controls.cpp's conversion for a parameter, the
// one ProcessOpenGL applies to it. NaN where the plugin uses the host's value
// as it is (Mix, clamped), an option or an event.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double bb_convert( int index, float v )
{
	switch( idOf( index ) )
	{
	case PT_THICKNESS: return ThicknessFromParam( v );
	case PT_DRAINAGE: return DrainageFromParam( v );
	case PT_SWIRL: return SwirlFromParam( v );
	case PT_BURST: return BurstFromParam( v );
	case PT_SLOW_MOTION: return SlowMotionFromParam( v );
	case PT_REFILL: return RefillFromParam( v );
	case PT_ANGLE: return AngleFromParam( v );
	case PT_EXPOSURE: return ExposureFromParam( v );
	default: return kNaN;
	}
}

/// And back: the host's 0..1 for a value in the plugin's units, by the
/// inverse Controls.cpp pairs with each conversion (the one its constructor
/// sets the defaults with). The page's presets are written in micrometres,
/// times, mm/s, seconds and degrees and go through this. NaN for a parameter
/// with no conversion.
EMSCRIPTEN_KEEPALIVE double bb_param_from( int index, double value )
{
	switch( idOf( index ) )
	{
	case PT_THICKNESS: return ParamFromThickness( value );
	case PT_DRAINAGE: return ParamFromDrainage( value );
	case PT_SWIRL: return ParamFromSwirl( value );
	case PT_BURST: return ParamFromBurst( value );
	case PT_SLOW_MOTION: return ParamFromSlowMotion( value );
	case PT_REFILL: return ParamFromRefill( value );
	case PT_ANGLE: return ParamFromAngle( value );
	case PT_EXPOSURE: return ParamFromExposure( value );
	default: return kNaN;
	}
}

//---------------------------------------------------------------------------
// What the line under the picture reports: the plugin's own state, through
// the accessors its harness reads, and Film.h's functions of it. It reports;
// it measures nothing.
//---------------------------------------------------------------------------
/// The film's clock (film::Clock): 0 phase (0 draining, 1 bursting, 2
/// empty), 1 generation, 2 age s, 3 tau s, 4 t0 s, 5 burst s (host time since
/// the hole nucleated), 6 empty s.
EMSCRIPTEN_KEEPALIVE double bb_clock( Instance* instance, int which )
{
	const film::Clock& c = instance->plugin.FilmClock();
	switch( which )
	{
	case 0: return c.phase == film::Phase::Draining ? 0.0 : c.phase == film::Phase::Bursting ? 1.0 : 2.0;
	case 1: return static_cast< double >( c.generation );
	case 2: return static_cast< double >( c.age ) * 1e-6;
	case 3: return c.tau;
	case 4: return c.t0;
	case 5: return static_cast< double >( c.burstMicros ) * 1e-6;
	case 6: return static_cast< double >( c.emptyMicros ) * 1e-6;
	default: return kNaN;
	}
}

/// The black front's height now (0 the foot, 1 the top): film::FrontHeight.
EMSCRIPTEN_KEEPALIVE double bb_front( Instance* instance )
{
	const film::Clock& c = instance->plugin.FilmClock();
	return film::FrontHeight( c.tau, c.t0 );
}

/// The film at mid-height, nm, where nothing has stirred it: A(tau) G(0.5).
EMSCRIPTEN_KEEPALIVE double bb_mid_nm( Instance* instance )
{
	const film::Clock& c = instance->plugin.FilmClock();
	return film::Amplitude( c.tau, c.t0 ) * film::Shape( 0.5 ) * 1e9;
}

/// The drainage time at which the film bursts at this Burst: film::BurstTime
/// (+inf for Burst 1, never).
EMSCRIPTEN_KEEPALIVE double bb_burst_tau( Instance* instance )
{
	const film::Clock& c = instance->plugin.FilmClock();
	return film::BurstTime( c.t0, BurstFromParam( instance->plugin.GetFloatParameter( PT_BURST ) ) );
}

/// How long the burst runs before the frame is empty, s of host time at this
/// Slow Motion: film::BurstDuration for the aspect, times Slow Motion.
EMSCRIPTEN_KEEPALIVE double bb_burst_span( Instance* instance, double aspect )
{
	return film::BurstDuration( aspect ) * SlowMotionFromParam( instance->plugin.GetFloatParameter( PT_SLOW_MOTION ) );
}

EMSCRIPTEN_KEEPALIVE double bb_steps_run( Instance* instance )
{
	return static_cast< double >( instance->plugin.StepsRun() );
}

EMSCRIPTEN_KEEPALIVE int bb_grid_width( Instance* instance )
{
	return instance->plugin.GridWidth();
}

EMSCRIPTEN_KEEPALIVE int bb_grid_height( Instance* instance )
{
	return instance->plugin.GridHeight();
}

/// How long the colour table took to sum (film::Tints().seconds), s. The
/// plugin builds it on its first frame, once per process.
EMSCRIPTEN_KEEPALIVE double bb_tint_seconds()
{
	return film::Tints().seconds;
}

/// Give the plugin's log (Diag.cpp) somewhere to go: a directory in the
/// page's in-memory file system, named through the override Diag.cpp reads.
/// Its own `mkdir -p` goes through system(), which a browser does not have.
/// Call before the first InitGL, which is when the log opens.
EMSCRIPTEN_KEEPALIVE void bb_prepare_log()
{
	setenv( "BUBBLE_LOG_DIR", "/bubble/logs", 1 );
	mkdir( "/bubble", 0755 );
	mkdir( "/bubble/logs", 0755 );
}

/// The plugin's log file (Diag.cpp), in the page's in-memory file system.
EMSCRIPTEN_KEEPALIVE const char* bb_log_path()
{
	static std::string path;
	path = diag::logPath();
	return path.c_str();
}

} // extern "C"
