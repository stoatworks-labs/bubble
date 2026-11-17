#include "Bubble.h"

/**
    The registration. `bubble_core` is an OBJECT library so this file-scope
    constructor, which nothing references by name, is linked into the bundle
    (an archive would let the linker drop it, and the bundle would load and
    report no plugins).
*/
static CFFGLPluginInfo PluginInfo(
	PluginFactory< bubble::BubblePlugin >,// Create method
	"BB01",                               // Plugin unique ID of maximum length 4
	"SW Bubble",                          // Plugin name
	2,                                    // API major version number
	1,                                    // API minor version number
	0,                                    // Plugin major version number
	1,                                    // Plugin minor version number
	FF_EFFECT,                            // Plugin type
	"The clip reflected in a draining soap film. The film's reflectance at each thickness is the interference of its two "
	"faces, summed over the spectrum against the CIE 1931 observer under D65; gravity drains it by Monier et al.'s "
	"self-similar law, so Newton's colours slide down from the top, a stirring carries them round, the thinnest film turns "
	"to black film, and then it bursts at the Taylor-Culick speed. CIE colour data CC BY-SA 4.0.",
	"Bubble FFGL effect"// About
);

extern "C" const char* BubbleBuildStamp()
{
	return "bubble " BUBBLE_VERSION ", built " __DATE__ " " __TIME__;
}
