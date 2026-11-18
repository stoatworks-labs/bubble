#!/usr/bin/env bash
#
# Build demo/bubble-core.{js,wasm}: the plugin's own C++, compiled to
# WebAssembly for the browser demo. (pinhole's script, which is fountain's and
# polyhedral's, with Bubble's files and its generated CIE include.)
#
#     demo/tools/build-wasm.sh          (needs emscripten: em++ on PATH, or EMXX=...;
#                                        and cmake, for the CIE include)
#
# The output is COMMITTED, because the Cloudflare deploy has no build step:
# what is in demo/ is what is served. So is demo/wasm/inputs.sha256, the hash
# of every input below and of both outputs; demo/tools/check_shaders.py (run
# by tools/verify.sh) fails when a source file has changed since this last ran,
# which is the only way a stale .wasm can be noticed without emscripten. Rerun
# it after anything under source/, data/ or cmake/CieTables.cmake changes -- a
# regenerated StoatworksAbout.h included: the About block's buttons are
# parameters too.
#
# What goes in, and none of it is edited for the browser:
#
#   source/   Diag, Film, Controls, PassBuffer, Shaders and Bubble.cpp -- the
#             plugin class itself, with its constructor, the host clock and its
#             vote, the film's clock (draining, bursting, empty), the colour
#             table summed in double from the CIE data, the G table, the R32F
#             displacement grid and its advection steps, every uniform it sets
#             and the GL state it saves and puts back. Everything but the
#             registration (EffectPlugin.cpp), whose job the page does.
#   CieTables.inc   generated from data/*.csv by cmake/CieTables.cmake's own
#             function, run here in cmake's script mode, exactly as the
#             plugin's CMakeLists.txt runs it at configure time (the CIE's
#             numbers copied as written; CC BY-SA 4.0, never edited). It is
#             written to a scratch directory and compiled into Film.cpp; the
#             two CSVs and the .cmake are hashed into the manifest.
#   external/ffgl   the FFGL SDK's CFFGLPluginManager / CFFGLPlugin (parameter
#             bookkeeping, read back by the page as a host reads it), FFGLLog,
#             and ffglex's FFGLShader, FFGLFBO (PassBuffer's base) and the
#             scoped bindings they use.
#   demo/wasm glue.cpp (the host's calls, as C functions) and gl_shim.cpp (the
#             GL entry points WebGL2 cannot take as they are).
#
# GL itself is emscripten's WebGL2 library, bound by the page to its own
# context. `-D__linux__` is for the SDK's FFGLPlatform.h, which knows Windows,
# Linux and macOS and stops the build on anything else: told it is Linux, the
# SDK includes <GL/glew.h>, which emscripten provides. It is given only to the
# files that include the SDK (Bubble.cpp, PassBuffer.cpp, glue.cpp and the
# SDK's own).
#
# No threads. Film.cpp sums the colour table's 121 rows on
# `hardware_concurrency() - 1` helper threads plus the calling one; this module
# is built without pthreads, where emscripten's libc reports ONE core, so the
# plugin starts no helper and its own loop sums every row on the page's thread.
# Each row is summed whole by one thread either way, so the table is the same;
# only the time it takes differs.
#
# Flags follow CMakeLists.txt's Release build: -O3 -DNDEBUG, C++17. WebAssembly
# has no fused multiply-add, so where clang fuses one on arm64 the browser
# rounds twice, and its pow, exp, log, sin and cos are musl's rather than
# Apple's libm: the colour table, the G table and A(tau) are the same sums in
# double, but need not agree with the Mac's bit for bit before they are
# rounded to float. Pictures against bbtest: AGENTS.md, "The browser demo".
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO"

EMXX="${EMXX:-em++}"
command -v "$EMXX" >/dev/null 2>&1 || { echo "em++ not found (install emscripten, or set EMXX=)" >&2; exit 1; }
command -v cmake >/dev/null 2>&1 || { echo "cmake not found: it generates the CIE include" >&2; exit 1; }
[[ -f external/ffgl/CMakeLists.txt ]] || { echo "FFGL SDK missing -- git submodule update --init --recursive" >&2; exit 1; }

version="$( sed -n 's/^[[:space:]]*VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | sed -n 1p )"
[[ -n "$version" ]] || { echo "no VERSION in CMakeLists.txt" >&2; exit 1; }

SDK=external/ffgl/source/lib
PLUGIN_SOURCES="Diag Film Controls PassBuffer Shaders Bubble"
SDK_SOURCES="ffgl/FFGLPluginManager ffgl/FFGLPluginSDK ffgl/FFGLLog ffglex/FFGLShader ffglex/FFGLFBO
             ffglex/FFGLScopedFBOBinding ffglex/FFGLScopedRenderBufferBinding ffglex/FFGLScopedTextureBinding
             ffglex/FFGLScopedSamplerActivation ffglex/FFGLScopedShaderBinding ffglex/FFGLScopedVAOBinding
             ffglex/FFGLScopedBufferBinding ffglex/FFGLUtilities"
# What cmake/CieTables.cmake reads (its foreach over the two specs).
CIE_INPUTS="cmake/CieTables.cmake data/CIE_xyz_1931_2deg.csv data/CIE_std_illum_D65.csv"

# Every source/*.cpp is either compiled here or the registration. A new file
# under source/ must be added to PLUGIN_SOURCES (or this line), not left out.
for file in source/*.cpp; do
	name="$( basename "$file" .cpp )"
	[[ "$name" == EffectPlugin || " $PLUGIN_SOURCES " == *" $name "* ]] \
		|| { echo "source/$name.cpp is not in PLUGIN_SOURCES -- add it, or say why it is left out" >&2; exit 1; }
done
# And the CIE function still reads exactly the two files hashed below.
for csv in data/*.csv; do
	[[ " $CIE_INPUTS " == *" $csv "* ]] || { echo "$csv is not in CIE_INPUTS -- does cmake/CieTables.cmake read it?" >&2; exit 1; }
	grep -qF "$( basename "$csv" )" cmake/CieTables.cmake || { echo "cmake/CieTables.cmake no longer names $csv" >&2; exit 1; }
done

OBJ="$( mktemp -d )"
trap 'rm -rf "$OBJ"' EXIT

#---------------------------------------------------------------------------
# The CIE include, by the repository's own function (CMakeLists.txt:
# bubble_cie_tables( source_dir, out_file )), in cmake's script mode.
#---------------------------------------------------------------------------
mkdir -p "$OBJ/generated"
cat > "$OBJ/cie.cmake" <<EOF
include( "$REPO/cmake/CieTables.cmake" )
bubble_cie_tables( "$REPO" "$OBJ/generated/CieTables.inc" )
EOF
cmake -P "$OBJ/cie.cmake"
[[ -s "$OBJ/generated/CieTables.inc" ]] || { echo "cmake/CieTables.cmake wrote no CieTables.inc" >&2; exit 1; }

INCLUDES=( -Isource -I"$SDK" -I"$OBJ/generated" )
BASE=( -std=c++17 -DNDEBUG -DBUBBLE_VERSION="\"$version\"" "${INCLUDES[@]}" )
SDK_PLATFORM=( -D__linux__=1 )

objects=()
compile() {
	local src="$1" opt="$2"; shift 2
	local obj="$OBJ/$( echo "$src" | tr '/' '_' ).o"
	"$EMXX" "${BASE[@]}" "$opt" "$@" -c "$src" -o "$obj"
	objects+=( "$obj" )
}

for name in $PLUGIN_SOURCES; do
	if [[ "$name" == Bubble || "$name" == PassBuffer ]]; then
		compile "source/$name.cpp" -O3 "${SDK_PLATFORM[@]}"
	else
		compile "source/$name.cpp" -O3
	fi
done
for name in $SDK_SOURCES; do
	compile "$SDK/$name.cpp" -O3 "${SDK_PLATFORM[@]}"
done
compile demo/wasm/glue.cpp -O3 "${SDK_PLATFORM[@]}"
compile demo/wasm/gl_shim.cpp -O3

LINK=(
	-O3
	-sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createBubble
	-sENVIRONMENT=web
	-sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576
	-sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
	-sDYNAMIC_EXECUTION=0
	-sFILESYSTEM=1
	-sEXPORTED_RUNTIME_METHODS=GL,FS,UTF8ToString,stringToNewUTF8
	-sEXPORTED_FUNCTIONS=_malloc,_free
)
"$EMXX" "${objects[@]}" "${LINK[@]}" -o demo/bubble-core.js
# emscripten leaves the .wasm executable; it is data.
chmod 644 demo/bubble-core.js demo/bubble-core.wasm

#---------------------------------------------------------------------------
# The manifest check_shaders.py reads. Inputs first, then outputs.
#---------------------------------------------------------------------------
{
	echo "# demo/tools/build-wasm.sh, $( "$EMXX" --version | sed -n 1p )"
	echo "# FFGL SDK $( git -C external/ffgl rev-parse HEAD )"
	echo "# CieTables.inc (generated, not kept) $( shasum -a 256 "$OBJ/generated/CieTables.inc" | cut -d' ' -f1 )"
	for name in $PLUGIN_SOURCES; do shasum -a 256 "source/$name.cpp"; done
	for header in source/*.h; do shasum -a 256 "$header"; done
	for input in $CIE_INPUTS; do shasum -a 256 "$input"; done
	for name in $SDK_SOURCES; do shasum -a 256 "$SDK/$name.cpp"; done
	shasum -a 256 demo/wasm/glue.cpp demo/wasm/gl_shim.cpp demo/tools/build-wasm.sh
	shasum -a 256 demo/bubble-core.js demo/bubble-core.wasm
} > demo/wasm/inputs.sha256

ls -l demo/bubble-core.js demo/bubble-core.wasm | awk '{ print "wrote " $NF ", " $5 " bytes" }'
