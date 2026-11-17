#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders, or its own copy of the physics, would pass every mutant here.
#
# One copy of the tree in a temporary directory (the FFGL SDK is symlinked,
# not copied), built arm64-only once. Each mutant edits its file in the copy,
# deletes that file's object -- every object, for a header -- and the binary
# (make compares mtimes to the second, so an edit within the same second as
# the last build would otherwise be missed, and a relink skipped would run the
# last mutant's bbtest), builds bbtest, requires a GLSL mutant to be in the
# binary verbatim, runs the named check at 320x180 under a time limit (a
# mutant can hang: honeydew's lesson; a timeout counts as caught), then puts
# the file back. --resize always runs its two rasters' pair and the offline
# --model ignores --size. (ferrofluid's script by way of fountain, filings,
# intensifier, convergence and lithophane; bubble's mutants.)
#
#     tools/mutate.sh          every mutant
#     ONLY=3 tools/mutate.sh   the third alone (ONLY=3,7 the third and seventh)
#
# Set TMPDIR to a directory you can write: the heredoc below hangs without it
# on this Mac.
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
#
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap): no GLSL || here. Mutants left out as equivalent or not
# covered are in AGENTS.md.
MUTANTS=(
	"source/Shaders.cpp|vec2 g   = X * vec2( FieldSize ) - 0.5;|vec2 g   = X * vec2( FieldSize ) - 0.6;|--resize|GLSL: the field read a tenth of a cell off, 5 -> 6"
	"source/Shaders.cpp|float lower = a + t.x * ( b - a );|float lower = a - t.x * ( b - a );|--resize|GLSL: the field's bilinear backwards along x, + -> -"
	"source/Shaders.cpp|float z  = 3.14159265358979 * ( x - k );|float z  = 3.14159265358979 * ( x + k );|--swirl|GLSL: sinCosPi's reduction the wrong way, - -> +"
	"source/Shaders.cpp|z2 * ( -1.0 / 6.0 + z2|z2 * ( -1.0 / 7.0 + z2|--swirl|GLSL: the sine's cubic term 1/7, 6 -> 7"
	"source/Shaders.cpp|vec2 two = vec2( 2.0 * one.x * one.y,|vec2 two = vec2( 3.0 * one.x * one.y,|--swirl|GLSL: sin 2 theta with a 3, 2 -> 3"
	"source/Shaders.cpp|* float( ModeQ[ k ] ) * a.x * b.y;|* float( ModeQ[ k ] ) * a.x * b.x;|--swirl|GLSL: the flow's x with sin for cos, y -> x"
	"source/Shaders.cpp|v.y -= Coefficient[ k ]|v.y += Coefficient[ k ]|--swirl|GLSL: the flow's y the wrong way (not divergence-free), - -> +"
	"source/Shaders.cpp|vec2 back = clamp( X - velocity( mid ), 0.0, 1.0 );|vec2 back = clamp( X + velocity( mid ), 0.0, 1.0 );|--swirl|GLSL: traced forward, not back, - -> +"
	"source/Shaders.cpp|fieldAt( back ) + ( back.y - X.y )|fieldAt( back ) + ( back.x - X.y )|--swirl|GLSL: the displacement from the wrong axis, y -> x"
	"source/Shaders.cpp|/ 1.055, vec3( 2.4 ) );|/ 1.055, vec3( 2.5 ) );|--alpha|GLSL: the sRGB decode's exponent 2.5, 4 -> 5"
	"source/Shaders.cpp|vec3( 1.0 / 2.4 ) ) - 0.055;|vec3( 1.0 / 2.4 ) ) - 0.065;|--colour|GLSL: the sRGB encode's offset 0.065, 5 -> 6"
	"source/Shaders.cpp|vec4( 0.0 ), isNaN );|vec4( 1.0 ), isNaN );|--robust|GLSL: a NaN read as 1, not 0, 0 -> 1"
	"source/Shaders.cpp|int row = y < 0.5 ? 0 : 1;|int row = y < 0.6 ? 0 : 1;|--black|GLSL: the G table's lower row read to 0.6, 5 -> 6"
	"source/Shaders.cpp|sqrt( sqrt( sqrt( max( v, 0.0 ) ) ) )|sqrt( sqrt( sqrt( max( v, 1.0 ) ) ) )|--drainage|GLSL: the G table read at its end, 0 -> 1"
	"source/Shaders.cpp|if( Transition == 1 && h < CriticalNm )|if( Transition == 1 && h > CriticalNm )|--black|GLSL: the black film above h_cr, < -> >"
	"source/Shaders.cpp|float x  = h * CosRef / TintStep;|float x  = h * CosRef * TintStep;|--colour|GLSL: the colour table's column times its step, / -> *"
	"source/Shaders.cpp|return lower + TintRowT * ( upper - lower );|return lower - TintRowT * ( upper - lower );|--colour|GLSL: the angle's rows interpolated backwards, + -> -"
	"source/Shaders.cpp|sum += SpeedModel == 1 ?|sum += SpeedModel == 0 ?|--burst|GLSL: the wrong speed model in the plugin, 1 -> 0"
	"source/Shaders.cpp|* InverseSpeed * sum / 48.0;|* InverseSpeed * sum / 49.0;|--burst|GLSL: the march's mean over 49, 8 -> 9"
	"source/Shaders.cpp|bool open = Phase == 1 &&|bool open = Phase == 2 &&|--burst|GLSL: the hole only while empty, 1 -> 2"
	"source/Shaders.cpp|if( Phase != 2 )|if( Phase != 1 )|--refill|GLSL: the film shown while empty, 2 -> 1"
	"source/Shaders.cpp|toCode( Exposure * film * lin );|toCode( Exposure + film * lin );|--colour|GLSL: Exposure added, * -> +"
	"source/Shaders.cpp|AlphaModel == 1 ? a : 1.0;|AlphaModel == 1 ? a : 0.0;|--alpha|GLSL: the film transparent, 1 -> 0"
	"source/Shaders.cpp|src.rgb * a * ( 1.0 - m )|src.rgb * a * ( 2.0 - m )|--alpha|GLSL: the clip's share at Mix m 2 - m, 1 -> 2"
	"source/Shaders.cpp|if( MixAmount <= 0.0 )|if( MixAmount <= 0.5 )|--alpha|GLSL: the clip handed through up to Mix 0.5, 0 -> 5"
	"source/Shaders.cpp|lin *= a;|lin /= a;|--alpha|GLSL: the clip's light divided by its alpha, * -> /"
	"source/Shaders.cpp|vec2 X    = ( vec2( at ) + 0.5 ) / vec2( Size );|vec2 X    = ( vec2( at ) + 0.6 ) / vec2( Size );|--burst|GLSL: the pixel's centre a tenth off, 5 -> 6"
	"source/Film.cpp|const double F = 4.0 * R1|const double F = 5.0 * R1|--model|C++: Airy's coefficient of finesse with 5, 4 -> 5"
	"source/Film.cpp|return F * s / ( 1.0 + F * s );|return F * s / ( 2.0 + F * s );|--model|C++: Airy's denominator 2 + F s, 1 -> 2"
	"source/Film.cpp|const double rp = ( i.n * c1 - i.cosT )|const double rp = ( i.n * c1 + i.cosT )|--model|C++: Fresnel's r_p numerator, - -> +"
	"source/Film.cpp|s2              = s2 * cr + c2 * sr;|s2              = s2 * cr - c2 * sr;|--model|C++: the table's phase rotation the wrong way, + -> -"
	"source/Film.cpp|const double stepH = kTintStepNm / CosRefracted( theta );|const double stepH = kTintStepNm * CosRefracted( theta );|--model|C++: a row's thickness step times cos, / -> *"
	"source/Film.cpp|wz = s65 * kCmf[ k ][ 3 ];|wz = s65 * kCmf[ k ][ 2 ];|--model|C++: the table's Z summed with ybar, 3 -> 2"
	"source/Film.cpp|rgb[ r ] = v / w.rgb[ r ];|rgb[ r ] = v * w.rgb[ r ];|--model|C++: the white correction multiplied, / -> *"
	"source/Film.cpp|{ 3.2406, -1.5372, -0.4986 },|{ 3.2407, -1.5372, -0.4986 },|--model|C++: the sRGB matrix's first entry 3.2407, 6 -> 7"
	"source/Film.cpp|const double T = ( celsius + 273.15 )|const double T = ( celsius + 273.16 )|--model|C++: IAPWS's reduced temperature off 0.01 K, 5 -> 6"
	"source/Film.cpp|* MasterInverse( 0.5 );|* MasterInverse( 0.6 );|--drainage|C++: t0 from the film at 0.6 of the height, 5 -> 6"
	"source/Film.cpp|std::log1p( -y ) / kFitC );|std::log1p( -y ) * kFitC );|--model|C++: the master curve's inverse with c, / -> *"
	"source/Film.cpp|nucleation[ 0 ] = 0.15 +|nucleation[ 0 ] = 0.16 +|--burst|C++: the hole's start a hundredth right, 5 -> 6"
	"source/Film.cpp|? 0.98 : 0.5 * ( 1.0 + front );|? 0.97 : 0.5 * ( 1.0 + front );|--burst|C++: a young film's hole a hundredth lower, 8 -> 7"
	"source/Film.cpp|return std::sqrt( 1.0 + aspect * aspect )|return std::sqrt( 2.0 + aspect * aspect )|--refill|C++: the burst's span on a wrong diagonal, 1 -> 2"
	"source/Film.cpp|std::max( s.refillSeconds, 0.0 ) * 1e6 );|std::max( s.refillSeconds, 0.0 ) * 1e5 );|--refill|C++: Refill in tenths, 6 -> 5"
	"source/Film.cpp|flow.bound += flow.weight[ k ] * kPi|flow.bound += flow.weight[ k ] / kPi|--swirl|C++: the speed bound over pi, * -> /"
	"source/Film.cpp|= a * kPi / ( W * H );|= a * kPi / ( W + H );|--swirl|C++: the mode's coefficient over W + H, * -> +"
	"source/Film.cpp|flow.frequency[ k ] = 0.05 + 0.25 *|flow.frequency[ k ] = 0.05 + 0.35 *|--swirl|C++: the modes' frequencies wider, 2 -> 3"
	"source/Film.cpp|/ drainageBase * 1e6;|/ drainageBase * 1e5;|--refill|C++: the burst's instant in tenths, 6 -> 5"
	"source/Film.cpp|return std::sqrt( kDensity * 1e-9 / ( 2.0 * kSurfaceTension ) );|return std::sqrt( kDensity * 1e-9 / ( 3.0 * kSurfaceTension ) );|--burst|C++: Taylor-Culick with 3 gamma, 2 -> 3"
	"source/Film.h|constexpr double kDensity        = 998.20715;|constexpr double kDensity        = 998.20716;|--model|C++: water's density a hundred-thousandth off, 5 -> 6"
	"source/Film.h|constexpr int kStepRate = 720;|constexpr int kStepRate = 730;|--rate|C++: 730 steps a second, 2 -> 3"
	"source/Film.h|constexpr double kCriticalNm    = 30.0;|constexpr double kCriticalNm    = 31.0;|--drainage|C++: h_cr 31 nm, 0 -> 1"
	"source/Film.h|constexpr double kCommonBlackNm = 12.0;|constexpr double kCommonBlackNm = 13.0;|--black|C++: the common black film 13 nm, 2 -> 3"
	"source/Film.h|constexpr double kExponentN = 0.72;|constexpr double kExponentN = 0.73;|--model|C++: the drainage exponent 0.73, 2 -> 3"
	"source/Film.h|constexpr double kKappa     = 2.4e-4;|constexpr double kKappa     = 2.5e-4;|--drainage|C++: kappa 2.5e-4, 4 -> 5"
	"source/Film.h|constexpr double kSurfaceTension = 0.035;|constexpr double kSurfaceTension = 0.036;|--burst|C++: gamma 0.036 N/m, 5 -> 6"
	"source/Bubble.cpp|constexpr int64_t kMaxFrameMicros = 100000;|constexpr int64_t kMaxFrameMicros = 200000;|--clock|C++: a frame may run 0.2 s, 1 -> 2"
	"source/Bubble.cpp|const double rowAt   = degrees / film::kTintAngleStep;|const double rowAt   = degrees * film::kTintAngleStep;|--colour|C++: the angle's row times its step, / -> *"
	"source/Bubble.cpp|clock.burstMicros ) * 1e-6 / slow|clock.burstMicros ) * 1e-5 / slow|--burst|C++: the burst's time in tenths of a second, 6 -> 5"
	"source/Bubble.cpp|film::kStepRate, 1.0 / film::kStepRate );|film::kStepRate, 2.0 / film::kStepRate );|--swirl|C++: each step twice its time, 1 -> 2"
	"source/Bubble.cpp|if( clock.generation != fieldGeneration )|if( clock.generation == fieldGeneration )|--refill|C++: a new film keeps the old one's field, ! -> ="
	"source/Bubble.cpp|const int64_t micros = std::llround( now * 1e6 );|const int64_t micros = std::llround( now * 1e5 );|--drainage|C++: the host's time in tenths of a millisecond, 6 -> 5"
	"source/Bubble.cpp|const bool rising = value > 0.5f|const bool rising = value > 1.5f|--burst|C++: Pop never latches, 0 -> 1"
	"source/Controls.cpp|0.0, 1.0 ) : 0.0;|0.0, 1.0 ) : 1.0;|--model|C++: a NaN control read as its top, 0 -> 1"
	"source/Controls.h|constexpr double kSwirlHigh      = 20.0;|constexpr double kSwirlHigh      = 30.0;|--model|C++: Swirl's top 30 mm/s, 2 -> 3"
	"source/Controls.h|kSlowHigh = 1000.0;|kSlowHigh = 2000.0;|--model|C++: Slow Motion's top 2000, 1 -> 2"
	"source/GLState.h|viewport[ 2 ], viewport[ 3 ] );|viewport[ 2 ], viewport[ 2 ] );|--glstate|C++: the host's viewport put back square, 3 -> 2"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

tree="$WORK/tree"
mkdir -p "$tree/external"
cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/data" "$REPO/CMakeLists.txt" "$tree/"
ln -s "$REPO/external/ffgl" "$tree/external/ffgl"
cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DBUBBLE_BUILD_TOOLS=ON >/dev/null
cmake --build "$tree/build" --target bbtest -j6 >/dev/null 2>&1 || { echo "the unmutated copy does not build"; exit 1; }

caught=0
ran=0
index=0
for entry in "${MUTANTS[@]}"; do
	index=$(( index + 1 ))
	[[ -n "${ONLY:-}" && ",$ONLY," != *",$index,"* ]] && continue
	ran=$(( ran + 1 ))
	IFS='|' read -r file original mutant check what <<<"$entry"
	cp "$REPO/$file" "$tree/$file"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	# A header's mutant reaches every object that includes it: delete them all.
	object="$( basename "$file" ).o"
	[[ "$file" == *.h ]] && object="*.o"
	find "$tree/build" -name "$object" -delete
	# The binary too: a relink skipped would run the last mutant's bbtest.
	rm -f "$tree/build/bbtest"
	printf '\n== mutant %d: %s\n' "$index" "$what"
	status=0
	if ! cmake --build "$tree/build" --target bbtest -j6 >"$WORK/build.log" 2>&1; then
		# A mutant that does not compile proves nothing about the checks.
		printf '   FAIL  the mutant does not build -- choose another\n'
		tail -5 "$WORK/build.log" | sed 's/^/        /'
	elif [[ "$file" == source/Shaders.cpp ]] && ! LC_ALL=C grep -qF -- "$mutant" "$tree/build/bbtest"; then
		# A GLSL mutant is a string literal: it must be in the binary, verbatim.
		printf '   FAIL  the mutant is not in the built binary -- the build did not pick it up\n'
	else
		limit 600 "$tree/build/bbtest" "$check" --size 320x180 >"$WORK/log" 2>&1 || status=$?
		if [[ "$status" -eq 0 ]]; then
			printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
		else
			printf '   ok    %s fails against the mutant (exit %d%s):\n' "$check" "$status" "$( [[ "$status" -eq 142 ]] && echo ', the time limit' )"
			# A mutant that timed out printed no FAIL: grep finding none must not end the run.
			{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
			caught=$(( caught + 1 ))
		fi
	fi
	cp "$REPO/$file" "$tree/$file"
	find "$tree/build" -name "$object" -delete
done

printf '\nmutants: %d, caught: %d\n' "$ran" "$caught"
[[ "$ran" -gt 0 && "$caught" -eq "$ran" ]]
