#pragma once

#include "Controls.h"
#include "Film.h"
#include "PassBuffer.h"

#include <FFGLSDK.h>
// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace bubble
{

/// The harness's negative controls and test seams. All off in the plugin.
struct PluginHooks
{
	int opticsModel    = 0;    ///< film::OpticsModel: 1 no half-wave change, 2 no dispersion, 3 s only
	int drainageModel  = 0;    ///< film::DrainageModel: 1 n = 1, 2 t0 ignored
	int clockModel     = 0;    ///< 1 a fixed 1/60 s a frame; 2 the host's time read in float
	int stepModel      = 0;    ///< 1 one advection step a frame, of the frame's interval
	int flowModel      = 0;    ///< 1 the flow's sign flipped; 2 Swirl ignored
	bool clearOnResize = false;///< a new raster restarts the film
	int speedModel     = 0;    ///< 1 1/u proportional to h, not to its square root
	bool newFilmIgnored = false;
	bool noTransition  = false;///< the law followed below h_cr: no black film
	int alphaModel     = 0;    ///< 1 the clip's alpha passed through
	bool premultiply   = true; ///< false: the clip's straight colour reflected
	bool clampOn       = true; ///< false: the clip's NaN and range not tamed
	bool leakGLState   = false;
	double forceNm     = -1.0; ///< >= 0: a uniform film of this thickness (the --colour and --burst seam)
};

/**
    The plugin, `SW Bubble`: the clip reflected in a draining soap film.
    Every frame: the clock (whole microseconds), the film's clock
    (film::Clock: draining, bursting, empty), the advection steps that are
    due, then the composite (Shaders.h).

    Host indices are the ParamIds, About last.
*/
class BubblePlugin : public CFFGLPlugin
{
public:
	BubblePlugin();

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// The base class's stub fails, and a failed default deletes the instance
	/// -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;

	void SetById( unsigned int id, float value )
	{
		SetFloatParameter( id, value );
	}
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	PluginHooks& HooksForTest()
	{
		return hooks;
	}
	/// Settle the host clock's unit (1 seconds, 0.001 milliseconds) without
	/// the vote, which a harness running faster than real time never wins.
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	const film::Clock& FilmClock() const
	{
		return clock;
	}
	GLuint FieldTextureID() const
	{
		return field[ current ].TextureID();
	}
	int GridWidth() const
	{
		return gridWidth;
	}
	int GridHeight() const
	{
		return gridHeight;
	}
	int64_t StepsRun() const
	{
		return stepsRun;
	}
	uint32_t FrameIndex() const
	{
		return frameIndex;
	}

private:
	void updateClock();
	bool ensureGrid( int width, int height );
	void clearField();
	void advect( const double coefficient[ film::kModes ], const film::Flow& flow, bool moving, GLuint sourceTexture, int sourceW, int sourceH );
	void composite( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height, double aspect );

	GLint where( const ffglex::FFGLShader& shader, const char* name );

	float params[ PT_COUNT ] = {};
	PluginHooks hooks;
	uint32_t frameIndex = 0;
	bool pendingPop = false, pendingNewFilm = false;

	// The host clock (nipkow's: ferrofluid's vote on seconds or milliseconds,
	// fountain's offline fallback), kept in whole microseconds.
	double hostTime     = -1.0;
	double clockScale   = 0.0;
	double lastRawTime  = -1.0;
	double lastWallTime = -1.0;
	double wallStart    = -1.0;
	int secondsVotes    = 0;
	int millisVotes     = 0;
	int clockSamples    = 0;
	int clockFrames     = 0;
	bool settledJump    = false;
	double now          = 0.0;
	bool haveLast       = false;
	int64_t lastMicros  = 0;

	// The film.
	film::Clock clock;
	film::Flow flow;
	uint32_t flowGeneration = 0xffffffffu;
	double flowAspect       = 0.0;
	int64_t stepsDone       = 0;
	int64_t stepsRun        = 0;
	bool fieldDirty         = false;
	uint32_t fieldGeneration = 0;
	int lastWidth = 0, lastHeight = 0;

	ffglex::FFGLShader advectShader, compositeShader;
	GLuint vao            = 0;
	GLuint tintTexture    = 0;
	int tintModel         = -1;
	GLuint shapeTexture   = 0;
	int shapeModel        = -1;
	std::vector< std::pair< std::pair< GLuint, const char* >, GLint > > uniformCache;

	PassBuffer field[ 2 ];
	int current    = 0;
	int gridWidth  = 0;
	int gridHeight = 0;
};

} // namespace bubble
