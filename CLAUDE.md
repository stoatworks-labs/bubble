# bubble

The clip reflected in a draining soap film, for Resolume Arena/Avenue as one FFGL effect.
The film is water (IAPWS R9-97 at 20 °C) with air on both sides; its colour is the Airy
reflectance summed over the CIE 1931 2° functions under D65 at 1 nm; it thins from the top by
Monier et al.'s self-similar drainage law (2024); below 30 nm it is black film (12 nm Common
or 4 nm Newton); a chosen six-mode stirring carries it about; it bursts at the Taylor–Culick
speed and is pulled again. C++/GLSL, CMake MODULE → universal `.bundle` (macOS). MIT code;
the CIE tables and the colour table summed from them are CC BY-SA 4.0.

| id | display name | bundle | type |
| --- | --- | --- | --- |
| `BB01` | `SW Bubble` | `Bubble.bundle`, `com.stoatworks.ffgl.bubble` | effect |

Read `AGENTS.md` before touching the law's fit or its tables (`Film.cpp`: `f`, `finv`, the G
table's eighth-root warp), the film's clock (`film::Clock`, the 720 Hz step clock, the burst
and refill phases), the advection (`Shaders.cpp`: advect, the box modes' recurrence), the
composite's black-film and burst tests, or Exposure's default. The spec is
`~/Projects/resolume/specs/SPEC-bubble.md` and is kept in step with this repo.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUBBLE_BUILD_TOOLS=ON`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (into Arena's Extra Effects).
  Never from a shared session on this Mac: nothing here has been loaded into Arena.
- Render offline: `./build/bbtest --out /tmp/b.png --frames 600 --fps 60` (the defaults over
  the harness's card, ten seconds into the film's life)
- A clip of your own: `--clip clip.rgba` (raw RGBA at `--size`, top row first)
- List parameters: `./build/bbtest --list`
- Set anything by name: `./build/bbtest --set "Black Film=Newton" --set "Swirl=0.5"`
  (a standard control takes the host's 0..1; an option takes its name; anything unparseable,
  nan and inf included, is refused with exit 2). An event at a frame: `--at 30 "Pop=1"`.
- A clip through it: `ffmpeg -nostdin -i in.mov -f rawvideo -pix_fmt rgba - | ./build/bbtest --pipe --size WxH | ffmpeg -f rawvideo -pix_fmt rgba -s WxH -r 30 -i - out.mp4`
  (`--pipe` runs the film's clock at `--fps`, default 60; `--film N` renders N frames over the card)
- `--stats` prints each frame's 99th-percentile output luminance and brightest channel
  (linear, from the float readback) on stderr, and the same pooled over every draining frame
  at the end.
- Exposure's default, re-measured: `python3 tools/clips.py defaults` (all 33 of Arena's demo
  clips at 320x180, 330 frames each at 10 fps). The alpha check on two of them:
  `python3 tools/clips.py alpha`.

## Verify
- Everything: `TMPDIR=<a writable dir> OXBOW=/path/to/oxbow tools/verify.sh` (GLSL reserved
  words, `glslc` on all 3 shaders, the submodule pin, a fresh universal build with no warnings
  outside external/ffgl, lipo, plist, ad-hoc signature, `oxbow probe` and `selftest` (required,
  by name: there is no default path, and no oxbow is a failure); every check at 1280x720 and
  320x180; every check again on Apple's software renderer at 320x180; `--offline`; the alpha
  check and Exposure's default on Arena's clips; `--pipe` and `--set`'s refusals; the negative
  controls; the mutants; the sweep, plain and `--bare`; the bench). `KEEP_BUILD=1` reuses
  `build-verify/`. It needs Resolume Arena's demo clips in `/Applications/Resolume Arena/media`.
- Every check at once: `./build/bbtest --all`. One at a time (`--size WxH`: that raster alone):
  - **No GL**: `--model` (IAPWS against its own Table 3, the plugin's n against the
    harness's, Airy against a 2×2 transfer matrix, Pugh's 102/204 nm, the colour table at and
    between its nodes, the G table, the drainage fit, the film's clock at 24/60/144 fps, the
    conversions).
  - **Optics**: `--colour` (uniform films through the `forceNm` seam, every pixel against the
    harness's own CIE × D65 × transfer-matrix tint), `--black` (where the law is under 30 nm,
    exactly the 12 nm or 4 nm film).
  - **The law**: `--drainage` (Swirl 0: the black front's row in every column against the
    law, at three settings and four ages), `--rate` (24, 60, 144 fps bit-identical with Swirl
    on), `--clock` (a host clock from 4.99e8 ms, stalled, jumping back and forward),
    `--swirl` (the black mask against origins traced back in double through the harness's own
    flow; the band derived from the scheme's diffusion).
  - **The burst**: `--burst` (the hole against Taylor–Culick at 12, 300 and 2000 nm),
    `--refill` (Empty for Refill seconds, then a film equal to a fresh one; New Film).
  - **Host**: `--resize` (same aspect: bit-exact; aspect change: the field resampled),
    `--alpha`, `--glstate`, `--robust` (a wild clip renders as its tamed copy; every control at
    its ends is finite).
- The checks can fail: `./build/bbtest --negative` (22 wrong models), `tools/mutate.sh`
  (65 one-character mutants, arm64, 320x180, perl alarm at 600 s; `ONLY=n` runs one; set
  TMPDIR first).
- No dead controls: `python3 tools/sweep.py` (each control in a context where it acts;
  `--bare` drops the context).
- Render cost: `./build/bbtest --bench` (720p and 1080p still and stirred, one 4K run).
- `BBTEST_RENDERER=software` runs any of it on Apple's software renderer.
- Shared Mac: arm64 builds while iterating, no 4K loops, never kill a process you didn't start.

## Notes
- **Two shaders a frame at most** (`Shaders.h`, three for `glslc`): advect (R32F ping-pong,
  one pass per 1/720 s step of film age, only while Swirl > 0 or the field is not zero), then
  composite (the field by hand bilinear, the G table, black film, the burst's 48-tap arrival
  march, the tint table, Exposure, the sRGB encode, the premultiplied Mix). Nothing is blended.
- **The thickness is never stored.** The grid holds d = ζ − y, where each point's film came
  from; h = A(τ) · G(ζ) with A computed in double on the CPU. At Swirl 0 the field stays 0.
- **Time is whole microseconds of film age**, the sum of clamped host intervals; nothing reads
  the host's absolute time in a shader. Steps due = round(age × 720 / 10⁶).
- **Tables, not transcendentals, on the GPU**: tint 2001 × 121 RGBA32F, G 2 × 4096 R32F
  indexed by y^(1/8) from each end; the box modes' sines by polynomial and recurrence. Apple's
  software renderer's `sin`, `log` and `pow` are not trusted.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element index;
  Pop and New Film are rising edges.
- GLSL 4.10 core. Reserved words (`common partition active … packed sample patch layout flat`,
  `noise1`–`noise4`, `any far near fixed half`) must not be identifiers, and nothing writes
  `M_PI`; `verify.sh` greps for both. Every sampler a shader declares gets a complete texture
  bound, read or not, or Apple's driver logs "unloadable".
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `bubble_core` is an OBJECT library: the plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **Generated, never edited here:** `source/StoatworksAbout.h` (stoatworks-backend's
  `sync-about.py`, from the website's projects.json), `ATTRIBUTIONS.md` (`sync-attributions.py`,
  from the backend's `attributions/derived.json`), `.github/FUNDING.yml` and
  `.github/ISSUE_TEMPLATE/` (`sync-funding.sh`, `sync-issue-templates.py`). The About block is
  17 host parameters in all: the 12 controls, the About text and four buttons (User guide,
  Project page, Source on GitHub, Support the work).
- **Public, so Actions run here** (github.com/stoatworks-labs/bubble, public since
  2026-11-18). `.github/workflows/ci.yml` (convergence's) builds on macOS (arm64, then
  `bbtest --list`, `bbtest --offline` and `glslc`) and Windows (x64, GLEW from vcpkg) on every
  push, pull request and dispatch; `release.yml` builds the universal bundle and the Windows DLL
  (checked by `tools/wincheck.ps1`, which loads it as a host does) on a `v*` tag or a dispatch,
  and publishes only on a tag. The pixel checks need a GPU and run only in `tools/verify.sh`,
  which is still the verification.
- `~/Projects/resolume/bubble`. The CIE tables in `data/` are the CIE's, under CC BY-SA 4.0,
  not MIT (`data/README.md`, ATTRIBUTIONS.md); settled for release by filament's precedent
  (AGENTS.md).

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume). It logs
the GL renderer, a shader that fails to compile, the host clock's unit at frame 60 and how
long the tint table took.

    ~/Library/Logs/bubble/bubble.YYYY-MM-DD.log
    %LOCALAPPDATA%\bubble\logs\bubble.YYYY-MM-DD.log

`BUBBLE_LOG_DIR` overrides the folder.
