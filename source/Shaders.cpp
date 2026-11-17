#include "Shaders.h"

namespace bubble::shaders
{

const char* const kVersion = "#version 410 core\n";

/// The full-raster triangle, from gl_VertexID alone.
const char* const kQuadVertex = R"(
void main()
{
	vec2 p = vec2( float( ( gl_VertexID << 1 ) & 2 ), float( gl_VertexID & 2 ) );
	gl_Position = vec4( p * 2.0 - 1.0, 0.0, 1.0 );
}
)";

/// The displacement grid read by hand: cell-centred bilinear, clamped to the
/// edge. X is the frame's normalised position, 0..1 both ways.
const char* const kField = R"(
uniform sampler2D Field;  // R32F: the origin height's displacement, in frame heights
uniform ivec2 FieldSize;

float fieldAt( vec2 X )
{
	vec2 g   = X * vec2( FieldSize ) - 0.5;
	vec2 lo  = clamp( floor( g ), vec2( 0.0 ), vec2( FieldSize - ivec2( 1 ) ) );
	vec2 t   = clamp( g - lo, 0.0, 1.0 );
	ivec2 i0 = ivec2( lo );
	ivec2 i1 = min( i0 + ivec2( 1 ), FieldSize - ivec2( 1 ) );
	float a  = texelFetch( Field, ivec2( i0.x, i0.y ), 0 ).r;
	float b  = texelFetch( Field, ivec2( i1.x, i0.y ), 0 ).r;
	float c  = texelFetch( Field, ivec2( i0.x, i1.y ), 0 ).r;
	float d  = texelFetch( Field, ivec2( i1.x, i1.y ), 0 ).r;
	float lower = a + t.x * ( b - a );
	float upper = c + t.x * ( d - c );
	return lower + t.y * ( upper - lower );
}
)";

/// sin and cos of pi x by the plugin's own polynomial (Taylor to z^11 and
/// z^12 on |z| <= pi/2 after reducing x to the nearest integer: truncation
/// under 6e-8), not the GPU's sin: Apple's software renderer's is not
/// accurate enough (nipkow).
const char* const kSinCos = R"(
vec2 sinCosPi( float x )
{
	float k  = floor( x + 0.5 );
	float z  = 3.14159265358979 * ( x - k );
	float z2 = z * z;
	float s  = z * ( 1.0 + z2 * ( -1.0 / 6.0 + z2 * ( 1.0 / 120.0 + z2 * ( -1.0 / 5040.0 + z2 * ( 1.0 / 362880.0 + z2 * ( -1.0 / 39916800.0 ) ) ) ) ) );
	float c  = 1.0 + z2 * ( -0.5 + z2 * ( 1.0 / 24.0 + z2 * ( -1.0 / 720.0 + z2 * ( 1.0 / 40320.0 + z2 * ( -1.0 / 3628800.0 + z2 * ( 1.0 / 479001600.0 ) ) ) ) ) );
	float sign = ( int( k ) & 1 ) == 0 ? 1.0 : -1.0;
	return sign * vec2( s, c );
}
)";

/// One step of the stirring, or (Moving 0) a resample from a grid of another
/// size. Coefficient[k] is c_k dt for the step's midpoint in time.
const char* const kAdvectFragment = R"(
uniform ivec2 GridSize;
uniform float Coefficient[ 6 ];
uniform int ModeP[ 6 ];
uniform int ModeQ[ 6 ];
uniform int Moving;
out vec4 FragColor;

vec2 multiple( vec2 one, int n )
{
	// sin and cos of n theta from those of theta, n = 1, 2, 3.
	vec2 two = vec2( 2.0 * one.x * one.y, one.y * one.y - one.x * one.x );
	if( n == 1 )
		return one;
	if( n == 2 )
		return two;
	return vec2( two.x * one.y + two.y * one.x, two.y * one.y - two.x * one.x );
}

// The displacement of X over the step, in normalised units.
vec2 velocity( vec2 X )
{
	vec2 sx = sinCosPi( X.x );
	vec2 sy = sinCosPi( X.y );
	vec2 v  = vec2( 0.0 );
	for( int k = 0; k < 6; ++k )
	{
		vec2 a = multiple( sx, ModeP[ k ] );
		vec2 b = multiple( sy, ModeQ[ k ] );
		v.x += Coefficient[ k ] * float( ModeQ[ k ] ) * a.x * b.y;
		v.y -= Coefficient[ k ] * float( ModeP[ k ] ) * a.y * b.x;
	}
	return v;
}

void main()
{
	vec2 X = gl_FragCoord.xy / vec2( GridSize );
	if( Moving == 0 )
	{
		FragColor = vec4( fieldAt( X ), 0.0, 0.0, 1.0 );
		return;
	}
	vec2 mid  = X - 0.5 * velocity( X );
	vec2 back = clamp( X - velocity( mid ), 0.0, 1.0 );
	FragColor = vec4( fieldAt( back ) + ( back.y - X.y ), 0.0, 0.0, 1.0 );
}
)";

/// To the host.
const char* const kCompositeFragment = R"(
uniform sampler2D InputTexture;
uniform sampler2D Shape;      // 2 rows: G at y = w^8 and at y = 1 - w^8
uniform float ShapeScale;     // (rows' length - 1) / 0.5^(1/8)
uniform int ShapeSize;
uniform sampler2D Tint;       // linear sRGB tints: columns h cos(theta_t,ref) / TintStep, rows angle
uniform int TintColumns;
uniform float TintStep;       // nm
uniform float CosRef;
uniform int TintRow;          // the row below the angle
uniform float TintRowT;       // and the fraction of the way to the next
uniform int FieldOn;
uniform float AmplitudeNm;    // A(tau), nm
uniform float MaxNm;
uniform float CriticalNm;
uniform float BlackNm;
uniform int Transition;       // 1 the plugin; 0 the law followed down to 0 (negative control)
uniform float ForceNm;        // >= 0: the harness's uniform film
uniform int Phase;            // 0 draining, 1 bursting, 2 empty
uniform vec2 Nucleus;         // the hole's start, normalised
uniform vec2 FrameMetres;     // W, H
uniform float BurstSeconds;   // burst time over slow motion
uniform float InverseSpeed;   // 1/u per sqrt(nm), s/m
uniform int SpeedModel;       // 0 Taylor-Culick; 1 1/u proportional to h (negative control)
uniform ivec2 ViewOrigin;
uniform ivec2 Size;
uniform float Exposure;
uniform float MixAmount;
uniform int AlphaModel;       // 0 the plugin; 1 the clip's alpha passed through
uniform int Premultiply;      // 1 the plugin; 0 the clip's straight colour
uniform int ClampOn;          // 1 the plugin; 0 the clip's NaN and range not tamed
out vec4 FragColor;

vec3 toLinear( vec3 c )
{
	c       = max( c, vec3( 0.0 ) );
	vec3 lo = c / 12.92;
	vec3 hi = pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) );
	return mix( lo, hi, step( vec3( 0.04045 ), c ) );
}
vec3 toCode( vec3 c )
{
	c       = clamp( c, 0.0, 1.0 );
	vec3 lo = c * 12.92;
	vec3 hi = 1.055 * pow( c, vec3( 1.0 / 2.4 ) ) - 0.055;
	return mix( lo, hi, step( vec3( 0.0031308 ), c ) );
}
vec4 tame( vec4 c )
{
	uvec4 bits  = floatBitsToUint( c );
	bvec4 isNaN = bvec4( uvec4( equal( bits & 0x7f800000u, uvec4( 0x7f800000u ) ) ) * uvec4( notEqual( bits & 0x007fffffu, uvec4( 0u ) ) ) );
	return mix( clamp( c, 0.0, 1.0 ), vec4( 0.0 ), isNaN );
}

// G( y ) from the table, by hand: row 0 below half height indexed by y^(1/8),
// row 1 above it by (1 - y)^(1/8) -- the warp that makes G nearly linear in
// the index at both ends (G ~ (1 - y)^0.123 at the top, a power of ln y at the
// foot).
float shapeAt( float y )
{
	int row = y < 0.5 ? 0 : 1;
	float v = row == 0 ? y : 1.0 - y;
	float x = sqrt( sqrt( sqrt( max( v, 0.0 ) ) ) ) * ShapeScale;
	float f = clamp( floor( x ), 0.0, float( ShapeSize - 2 ) );
	float t = clamp( x - f, 0.0, 1.0 );
	float lo = texelFetch( Shape, ivec2( int( f ), row ), 0 ).r;
	float hi = texelFetch( Shape, ivec2( int( f ) + 1, row ), 0 ).r;
	return lo + t * ( hi - lo );
}

// The film's thickness at X, nm, with the black film below h_cr.
float thicknessAt( vec2 X )
{
	if( ForceNm >= 0.0 )
		return ForceNm;
	float d = FieldOn == 1 ? fieldAt( X ) : 0.0;
	float y = clamp( X.y + d, 0.0, 1.0 );
	float h = min( AmplitudeNm * shapeAt( y ), MaxNm );
	if( Transition == 1 && h < CriticalNm )
		h = BlackNm;
	return h;
}

vec3 tintAt( float h )
{
	float x  = h * CosRef / TintStep;
	float c  = clamp( floor( x ), 0.0, float( TintColumns - 2 ) );
	float t  = clamp( x - c, 0.0, 1.0 );
	ivec2 a  = ivec2( int( c ), TintRow );
	vec3 a00 = texelFetch( Tint, a, 0 ).rgb;
	vec3 a10 = texelFetch( Tint, a + ivec2( 1, 0 ), 0 ).rgb;
	vec3 a01 = texelFetch( Tint, a + ivec2( 0, 1 ), 0 ).rgb;
	vec3 a11 = texelFetch( Tint, a + ivec2( 1, 1 ), 0 ).rgb;
	vec3 lower = a00 + t * ( a10 - a00 );
	vec3 upper = a01 + t * ( a11 - a01 );
	return lower + TintRowT * ( upper - lower );
}

// The hole's arrival time at X: the integral of 1/u along the straight
// segment from the nucleus, by the midpoint rule.
float arrival( vec2 X )
{
	float sum = 0.0;
	for( int i = 0; i < 48; ++i )
	{
		vec2 s  = Nucleus + ( X - Nucleus ) * ( ( float( i ) + 0.5 ) / 48.0 );
		float h = thicknessAt( s );
		sum += SpeedModel == 1 ? h / sqrt( 300.0 ) : sqrt( h );
	}
	return length( ( X - Nucleus ) * FrameMetres ) * InverseSpeed * sum / 48.0;
}

void main()
{
	ivec2 at = ivec2( gl_FragCoord.xy ) - ViewOrigin;
	vec4 src = texelFetch( InputTexture, at, 0 );
	if( MixAmount <= 0.0 )
	{
		FragColor = src;
		return;
	}
	if( ClampOn == 1 )
		src = tame( src );
	vec3 lin = toLinear( src.rgb );
	if( ClampOn == 1 )
		lin = clamp( lin, 0.0, 1.0 );
	float a = clamp( src.a, 0.0, 1.0 );
	if( Premultiply == 1 )
		lin *= a;

	vec2 X    = ( vec2( at ) + 0.5 ) / vec2( Size );
	vec3 film = vec3( 0.0 );
	if( Phase != 2 )
	{
		float h   = thicknessAt( X );
		bool open = Phase == 1 && arrival( X ) <= BurstSeconds;
		if( !open )
			film = max( tintAt( h ), vec3( 0.0 ) );
	}
	vec3 rgb   = toCode( Exposure * film * lin );
	float outA = AlphaModel == 1 ? a : 1.0;
	float m    = min( MixAmount, 1.0 );
	float mixA = a * ( 1.0 - m ) + outA * m;
	vec3 mixC  = ( src.rgb * a * ( 1.0 - m ) + rgb * outA * m ) / max( mixA, 1e-30 );
	FragColor  = vec4( mixC, mixA );
}
)";

std::string AssembleVertex( Pass )
{
	return std::string( kVersion ) + kQuadVertex;
}

std::string Assemble( Pass pass )
{
	switch( pass )
	{
	case Pass::Advect: return std::string( kVersion ) + kField + kSinCos + kAdvectFragment;
	case Pass::Composite: return std::string( kVersion ) + kField + kCompositeFragment;
	}
	return std::string();
}

} // namespace bubble::shaders
