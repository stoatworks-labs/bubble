#include "Bubble.h"

#include "Diag.h"
#include "GLState.h"
#include "Shaders.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

using namespace ffglex;

namespace bubble
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;
/// Frames of advancing host time after which an unsettled vote is decided by
/// the size of the host's step alone (updateClock).
constexpr int kClockPatience = 30;
/// Microseconds a single frame may run the film for.
constexpr int64_t kMaxFrameMicros = 100000;
/// The displacement grid's width; its height follows the aspect.
constexpr int kGridWidth = 512;

/// Texture units: the clip, the field, the shape table, the tint table.
constexpr int kUnitInput = 0, kUnitField = 1, kUnitShape = 2, kUnitTint = 3;
constexpr int kUnitsUsed = 4;

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

GLuint makeDataTexture()
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

void uploadData( GLuint texture, int width, int height, GLint internalFormat, GLenum layout, const float* texels )
{
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
	glPixelStorei( GL_UNPACK_SKIP_PIXELS, 0 );
	glPixelStorei( GL_UNPACK_SKIP_ROWS, 0 );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, width, height, 0, layout, GL_FLOAT, texels );
	glBindTexture( GL_TEXTURE_2D, 0 );
}

/// The grid's rows for a raster: the frame's aspect at kGridWidth columns.
int gridRowsFor( int width, int height )
{
	return std::max( 2, static_cast< int >( std::lround( static_cast< double >( kGridWidth ) * height / width ) ) );
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount, "the About run no longer matches StoatworksAbout.h" );
static_assert( shaders::kModes == film::kModes, "the advect pass reads one coefficient per mode" );

//---------------------------------------------------------------------------
BubblePlugin::BubblePlugin()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	//The film drains and the stirring moves in time.
	SetTimeSupported( true );

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( id, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Event: SetParamInfo( id, info.name, FF_TYPE_EVENT, false ); break;
		case Kind::Option:
			SetOptionParamInfo( id, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		}
		SetParamGroup( id, info.group );
	}

	// The About block, last. Inline rather than through a helper: SetParamInfo
	// is protected on CFFGLPlugin, so nothing outside the class can call it.
	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_TEXT; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );

	//The first film, generation 1.
	clock.Restart();
}

//---------------------------------------------------------------------------
GLint BubblePlugin::where( const FFGLShader& shader, const char* name )
{
	//Keyed by the literal's address: every name here is a string literal.
	const auto key = std::make_pair( shader.GetGLID(), name );
	for( const auto& entry : uniformCache )
		if( entry.first == key )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( key, location );
	return location;
}

//---------------------------------------------------------------------------
FFResult BubblePlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	struct Stage
	{
		FFGLShader* shader;
		shaders::Pass pass;
		const char* name;
	};
	const Stage list[] = {
		{ &advectShader, shaders::Pass::Advect, "advect" },
		{ &compositeShader, shaders::Pass::Composite, "composite" },
	};
	for( const Stage& stage : list )
	{
		const std::string vertex   = shaders::AssembleVertex( stage.pass );
		const std::string fragment = shaders::Assemble( stage.pass );
		if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. These lines are the only record of which
		//pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Bubble: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	//A core profile draws nothing without a vertex array bound; the
	//triangle needs no attributes.
	glGenVertexArrays( 1, &vao );
	tintTexture  = makeDataTexture();
	shapeTexture = makeDataTexture();
	tintModel = shapeModel = -1;
	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
void BubblePlugin::updateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending both seconds and milliseconds through
	//SetTime, and nothing in the call says which. Vote on it against the wall
	//clock for a few frames, then stop asking (nipkow's, which is
	//ferrofluid's).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;//wall time to the host's own: no interval across it
			}
		}
		//A host that renders much faster than real time -- an offline render,
		//oxbow's selftest -- never wins the vote. After kClockPatience frames
		//the step's size decides: a frame is well under a second and well
		//over a millisecond (fountain's).
		if( hostDelta > 0.0 && clockScale == 0.0 && ++clockSamples >= kClockPatience )
		{
			clockScale  = hostDelta < 1.0 ? 1.0 : 0.001;
			settledJump = true;
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	//Double throughout: Resolume's clock has been measured at ~499 million ms,
	//where a float resolves 0.03 s. Only differences are used.
	now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
	if( hooks.clockModel == 2 )
		now = static_cast< double >( static_cast< float >( now ) );
	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw ) + " scale=" + std::to_string( clockScale )
		            + " seconds=" + std::to_string( now ) );
}

//---------------------------------------------------------------------------
bool BubblePlugin::ensureGrid( int width, int height )
{
	const int rows = gridRowsFor( width, height );
	if( gridWidth == kGridWidth && gridHeight == rows && field[ 0 ].IsValid() && field[ 1 ].IsValid() )
		return true;
	const bool keep = fieldDirty && field[ current ].IsValid() && gridWidth > 0;
	const int oldW = gridWidth, oldH = gridHeight;
	const int other = 1 - current;
	if( !field[ other ].Ensure( kGridWidth, rows, GL_R32F, PassBuffer::Sampling::Nearest ) )
		return false;
	if( keep )
	{
		//A change of aspect: the state resampled to the new rows (bilinear).
		gridWidth  = kGridWidth;
		gridHeight = rows;
		const double none[ film::kModes ] = {};
		advect( none, flow, false, field[ current ].TextureID(), oldW, oldH );
		current = other;
	}
	gridWidth  = kGridWidth;
	gridHeight = rows;
	for( PassBuffer& b : field )
		if( !b.Ensure( kGridWidth, rows, GL_R32F, PassBuffer::Sampling::Nearest ) )
			return false;
	if( !keep )
		clearField();
	return true;
}

void BubblePlugin::clearField()
{
	for( PassBuffer& b : field )
		if( b.IsValid() )
			b.Clear();
	fieldDirty = false;
}

/// One pass into field[ 1 - current ], from `sourceTexture` (sourceW x
/// sourceH): a step of the stirring, or (moving false) a resample.
void BubblePlugin::advect( const double coefficient[ film::kModes ], const film::Flow& f, bool moving, GLuint sourceTexture, int sourceW, int sourceH )
{
	PassBuffer& target = field[ 1 - current ];
	glBindFramebuffer( GL_FRAMEBUFFER, target.GetGLID() );
	glViewport( 0, 0, gridWidth, gridHeight );
	glUseProgram( advectShader.GetGLID() );
	bindUnit( kUnitField, sourceTexture );
	glUniform1i( where( advectShader, "Field" ), kUnitField );
	glUniform2i( where( advectShader, "FieldSize" ), sourceW, sourceH );
	glUniform2i( where( advectShader, "GridSize" ), gridWidth, gridHeight );
	GLfloat c[ film::kModes ];
	GLint p[ film::kModes ], q[ film::kModes ];
	for( int k = 0; k < film::kModes; ++k )
	{
		c[ k ] = static_cast< GLfloat >( coefficient[ k ] );
		p[ k ] = f.p[ k ] > 0 ? f.p[ k ] : 1;
		q[ k ] = f.q[ k ] > 0 ? f.q[ k ] : 1;
	}
	glUniform1fv( where( advectShader, "Coefficient" ), film::kModes, c );
	glUniform1iv( where( advectShader, "ModeP" ), film::kModes, p );
	glUniform1iv( where( advectShader, "ModeQ" ), film::kModes, q );
	glUniform1i( where( advectShader, "Moving" ), moving ? 1 : 0 );
	glDrawArrays( GL_TRIANGLES, 0, 3 );
}

//---------------------------------------------------------------------------
void BubblePlugin::composite( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height, double aspect )
{
	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	glUseProgram( compositeShader.GetGLID() );
	bindUnit( kUnitInput, input->Handle );
	bindUnit( kUnitField, field[ current ].TextureID() );
	bindUnit( kUnitShape, shapeTexture );
	bindUnit( kUnitTint, tintTexture );
	const FFGLShader& s = compositeShader;
	glUniform1i( where( s, "InputTexture" ), kUnitInput );
	glUniform1i( where( s, "Field" ), kUnitField );
	glUniform2i( where( s, "FieldSize" ), gridWidth, gridHeight );
	glUniform1i( where( s, "FieldOn" ), fieldDirty ? 1 : 0 );
	glUniform1i( where( s, "Shape" ), kUnitShape );
	glUniform1f( where( s, "ShapeScale" ), static_cast< float >( ( film::kShapeSize - 1 ) / std::pow( 0.5, 0.125 ) ) );
	glUniform1i( where( s, "ShapeSize" ), film::kShapeSize );
	glUniform1i( where( s, "Tint" ), kUnitTint );
	glUniform1i( where( s, "TintColumns" ), film::kTintColumns );
	glUniform1f( where( s, "TintStep" ), static_cast< float >( film::kTintStepNm ) );

	const double degrees = AngleFromParam( params[ PT_ANGLE ] );
	const double theta   = degrees * film::kPi / 180.0;
	const double rowAt   = degrees / film::kTintAngleStep;
	const int row        = std::clamp( static_cast< int >( std::floor( rowAt ) ), 0, film::kTintRows - 2 );
	glUniform1f( where( s, "CosRef" ), static_cast< float >( film::CosRefracted( theta ) ) );
	glUniform1i( where( s, "TintRow" ), row );
	glUniform1f( where( s, "TintRowT" ), static_cast< float >( std::clamp( rowAt - row, 0.0, 1.0 ) ) );

	const double amplitude = film::Amplitude( clock.tau, clock.t0, hooks.drainageModel ) * 1e9;
	glUniform1f( where( s, "AmplitudeNm" ), static_cast< float >( std::min( amplitude, 1e15 ) ) );
	glUniform1f( where( s, "MaxNm" ), static_cast< float >( film::kMaxNm ) );
	glUniform1f( where( s, "CriticalNm" ), static_cast< float >( film::kCriticalNm ) );
	const bool newton = BlackFilmFromParam( params[ PT_BLACK_FILM ] ) == kNewtonBlack;
	glUniform1f( where( s, "BlackNm" ), static_cast< float >( newton ? film::kNewtonBlackNm : film::kCommonBlackNm ) );
	glUniform1i( where( s, "Transition" ), hooks.noTransition ? 0 : 1 );
	glUniform1f( where( s, "ForceNm" ), static_cast< float >( hooks.forceNm ) );
	const int phase = clock.phase == film::Phase::Draining ? 0 : clock.phase == film::Phase::Bursting ? 1 : 2;
	glUniform1i( where( s, "Phase" ), phase );
	glUniform2f( where( s, "Nucleus" ), static_cast< float >( clock.nucleation[ 0 ] ), static_cast< float >( clock.nucleation[ 1 ] ) );
	glUniform2f( where( s, "FrameMetres" ), static_cast< float >( film::kFilmHeight * aspect ), static_cast< float >( film::kFilmHeight ) );
	const double slow = SlowMotionFromParam( params[ PT_SLOW_MOTION ] );
	glUniform1f( where( s, "BurstSeconds" ), static_cast< float >( static_cast< double >( clock.burstMicros ) * 1e-6 / slow ) );
	glUniform1f( where( s, "InverseSpeed" ), static_cast< float >( film::InverseSpeedPerRootNm() ) );
	glUniform1i( where( s, "SpeedModel" ), hooks.speedModel );
	glUniform2i( where( s, "ViewOrigin" ), hostViewport[ 0 ], hostViewport[ 1 ] );
	glUniform2i( where( s, "Size" ), width, height );
	glUniform1f( where( s, "Exposure" ), static_cast< float >( ExposureFromParam( params[ PT_EXPOSURE ] ) ) );
	glUniform1f( where( s, "MixAmount" ), std::clamp( params[ PT_MIX ] == params[ PT_MIX ] ? params[ PT_MIX ] : 0.0f, 0.0f, 1.0f ) );
	glUniform1i( where( s, "AlphaModel" ), hooks.alphaModel );
	glUniform1i( where( s, "Premultiply" ), hooks.premultiply ? 1 : 0 );
	glUniform1i( where( s, "ClampOn" ), hooks.clampOn ? 1 : 0 );
	glDrawArrays( GL_TRIANGLES, 0, 3 );
}

//---------------------------------------------------------------------------
FFResult BubblePlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr || pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = pgl->inputTextures[ 0 ];

	//The host's state, put back however this returns.
	ScopedGLState restore;
	restore.restoreOnExit     = !hooks.leakGLState;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );

	const int width  = static_cast< int >( input->Width );
	const int height = static_cast< int >( input->Height );
	if( width <= 0 || height <= 0 || vao == 0 )
		return FF_FAIL;
	const double aspect = static_cast< double >( width ) / height;

	//The tables: built once per process (the tint table takes a moment the
	//first time), uploaded once per instance.
	if( tintModel != hooks.opticsModel )
	{
		const film::TintTable& t = film::Tints( hooks.opticsModel );
		uploadData( tintTexture, film::kTintColumns, film::kTintRows, GL_RGBA32F, GL_RGBA, t.rgba.data() );
		tintModel = hooks.opticsModel;
		diag::info( "tint table " + std::to_string( tintModel ) + " built in " + std::to_string( t.seconds ) + " s" );
	}
	if( shapeModel != hooks.drainageModel )
	{
		const std::vector< float > g = film::ShapeTable( hooks.drainageModel );
		uploadData( shapeTexture, film::kShapeSize, 2, GL_R32F, GL_RED, g.data() );
		shapeModel = hooks.drainageModel;
	}

	//The clock: the host's time in whole microseconds, the interval as the
	//difference of two, so it telescopes to the host's own elapsed time and a
	//clock at 4.99e8 ms gives the same intervals as one from 0.
	updateClock();
	if( settledJump )
	{
		haveLast    = false;
		settledJump = false;
	}
	const int64_t micros = std::llround( now * 1e6 );
	int64_t interval     = haveLast ? std::clamp< int64_t >( micros - lastMicros, 0, kMaxFrameMicros ) : 0;
	lastMicros           = micros;
	haveLast             = true;
	if( hooks.clockModel == 1 )
		interval = frameIndex == 0 ? 0 : 16667;

	glBindVertexArray( vao );

	//A new raster: the same aspect keeps the grid as it is; another aspect
	//resamples it.
	if( hooks.clearOnResize && lastWidth != 0 && ( width != lastWidth || height != lastHeight ) )
	{
		clock.Restart();
		clearField();
	}
	lastWidth  = width;
	lastHeight = height;
	if( !ensureGrid( width, height ) )
	{
		diag::error( "could not allocate the displacement grid" );
		return FF_FAIL;
	}

	//The film's clock.
	film::Settings settings;
	settings.thicknessUm   = ThicknessFromParam( params[ PT_THICKNESS ] );
	settings.drainage      = DrainageFromParam( params[ PT_DRAINAGE ] );
	settings.reach         = BurstFromParam( params[ PT_BURST ] );
	settings.slowMotion    = SlowMotionFromParam( params[ PT_SLOW_MOTION ] );
	settings.refillSeconds = RefillFromParam( params[ PT_REFILL ] );
	settings.aspect        = aspect;
	settings.drainageModel = hooks.drainageModel;
	const bool newFilm     = pendingNewFilm && !hooks.newFilmIgnored;
	const bool pop         = pendingPop;
	pendingNewFilm = pendingPop = false;
	const int64_t ageBefore     = clock.age;
	clock.Advance( interval, settings, pop, newFilm );
	if( clock.generation != fieldGeneration )
	{
		fieldGeneration = clock.generation;
		stepsDone       = 0;
		clearField();
	}
	if( flowGeneration != clock.generation || flowAspect != aspect )
	{
		flow           = film::FlowFor( clock.generation, aspect );
		flowGeneration = clock.generation;
		flowAspect     = aspect;
	}

	//The stirring's steps that are due: 1/720 s of film age each, the
	//coefficients at the step's midpoint in time. None while Swirl is 0.
	const double swirl = hooks.flowModel == 2 ? 0.0 : SwirlFromParam( params[ PT_SWIRL ] ) * 1e-3;
	const double sign  = hooks.flowModel == 1 ? -1.0 : 1.0;
	auto step = [ & ]( double tMid, double dt ) {
		double c[ film::kModes ];
		film::FlowCoefficients( flow, swirl, aspect, tMid, c );
		for( double& v : c )
			v *= sign * dt;
		advect( c, flow, true, field[ current ].TextureID(), gridWidth, gridHeight );
		current    = 1 - current;
		fieldDirty = true;
		++stepsRun;
	};
	if( hooks.stepModel == 1 )
	{
		//The negative control: one step a frame, of the frame's interval.
		if( swirl > 0.0 && clock.age > ageBefore )
		{
			const double dt = static_cast< double >( clock.age - ageBefore ) * 1e-6;
			step( static_cast< double >( ageBefore ) * 1e-6 + 0.5 * dt, dt );
		}
		stepsDone = clock.StepsDue();
	}
	else
	{
		const int64_t due = clock.StepsDue();
		if( swirl > 0.0 )
			for( int64_t n = stepsDone + 1; n <= due; ++n )
				step( ( static_cast< double >( n ) - 0.5 ) / film::kStepRate, 1.0 / film::kStepRate );
		stepsDone = std::max( stepsDone, due );
	}

	composite( input, hostViewport, pgl->HostFBO, width, height, aspect );
	unbindTextureUnits( kUnitsUsed );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
	++frameIndex;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult BubblePlugin::DeInitGL()
{
	uniformCache.clear();
	for( FFGLShader* s : { &advectShader, &compositeShader } )
		s->FreeGLResources();
	if( vao != 0 )
		glDeleteVertexArrays( 1, &vao );
	vao = 0;
	for( GLuint* t : { &tintTexture, &shapeTexture } )
	{
		if( *t != 0 )
			glDeleteTextures( 1, t );
		*t = 0;
	}
	tintModel = shapeModel = -1;
	for( PassBuffer& b : field )
		b.Destroy();
	gridWidth = gridHeight = 0;
	fieldDirty             = false;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult BubblePlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

char* BubblePlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult BubblePlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult BubblePlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// The About buttons open a browser and store nothing.
	if( index >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( index - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	//The events latch on their rising edge; the next frame consumes them.
	const bool rising = value > 0.5f && !( params[ index ] > 0.5f );
	if( index == PT_POP && rising )
		pendingPop = true;
	if( index == PT_NEW_FILM && rising )
		pendingNewFilm = true;
	params[ index ] = value;
	return FF_SUCCESS;
}

float BubblePlugin::GetFloatParameter( unsigned int index )
{
	return index < PT_COUNT ? params[ index ] : 0.0f;
}

} // namespace bubble
