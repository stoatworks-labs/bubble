# AGENTS.md: Bubble

Read this before changing the law's fit or its tables (`Film.cpp`: `Master`, `MasterInverse`,
the G table's eighth-root warp), the film's clock (`film::Clock`, the 720 Hz step clock, the
burst and refill phases), the advection (`Shaders.cpp`: advect, `sinCosPi`, `multiple`), the
composite's black-film and burst tests, or Exposure's default. `CLAUDE.md` has the commands;
the spec is `~/Projects/resolume/specs/SPEC-bubble.md`, written before any code and kept in step
with this repo (no As-built section: where the build departed from the first draft, the spec
was changed, and the changes are listed below). Everything here is as of 2026-11-17.

## The idea

**A soap film is a sheet of water a few hundred nanometres thick; light reflected from its two
faces interferes, so its colour is its thickness; gravity drains it from the top; under 30 nm
it jumps to black film, and then it bursts.** The picture is the clip, as the scene the film
reflects, tinted by the film: out = Exposure · tint(h, θ) ⊙ clip_lin · α. The tint is the Airy
reflectance of water (IAPWS R9-97 at 20 °C) for s and p, summed over the tabulated CIE 1931 2°
functions under D65 at 1 nm and white-balanced on D65. The thickness is Monier et al.'s
self-similar drainage law h = A(τ) · G(y), closed form in drainage time, so the film is frame-rate
independent by construction. A chosen six-mode stirring moves *where each piece of film came
from* (a displacement grid), never the thickness itself. Neighbours not duplicated: `holofoil`
(a grating; its CMF fit and equal-energy white), `pepper` (a coated foil, one wavelength per
channel).

## Decisions taken without asking (the brief says decide and write it down)

- **Identity.** `SW Bubble`, `BB01` (checked free against every `"BB01"` in
  `~/Projects/*/*/source` and `~/dev/*/source`), an effect, `com.stoatworks.ffgl.bubble`,
  harness `bbtest`. One class, `BubblePlugin`; host indices are the ParamIds, the About block
  last.
- **The state is the origin height, not the thickness.** A 512 × round(512 H/W) R32F grid
  holds d = ζ − y. At Swirl 0 it stays exactly 0, so the law holds exactly at every pixel at any
  raster, and the drainage claim is measurable to the row; the law is separable, so a moved
  piece of film keeps thinning at the law's rate.
- **Tables, not transcendentals, on the GPU.** Apple's software renderer's `sin`, `log` and
  `pow` are not trusted (nipkow): the tint is a 2001 × 121 RGBA32F table (optical thickness
  h cos θ_t,ref in 2 nm steps by 0.5° rows), G a 2 × 4096 R32F table indexed by v^{1/8}, the box
  modes' sines a Taylor polynomial on |z| ≤ π/2 with the multiples by recurrence. Everything is
  read by `texelFetch` and hand bilinear.
- **A fixed step clock of 720 Hz of film age** (the LCM of 24, 60 and 144 fps), age in whole
  microseconds, steps due = round(age × 720 / 10⁶) in int64; the stirring's coefficients are
  taken at each step's midpoint in time, computed in double on the CPU.
- **The master curve f** is an exponentiated-Weibull fit (a 0.13145, b 0.48916, c 23.08939) to
  ten points digitised from Monier et al.'s Fig. 3b, constrained to f(1) = 0.8 and f′(1) = −0.44;
  chosen for its closed-form inverse. Residuals rms 0.0090, worst 0.0173.
- **h_cr = 30 nm** (chosen within Pugh's ~50 nm and Monier's "below 50 nm"), black film 12 nm
  (Common: Reinold & Rücker via Ziapkoff et al.) or 4 nm (Newton: Pugh), a step with no gradient.
- **The burst.** Taylor–Culick u = √(2γ/(ρh)), γ = 35 mN/m (Monier's solutions), ρ = 998.20715;
  arrival times by a 48-tap midpoint march along the straight segment from the nucleus through
  the film frozen at the burst; the nucleus at x = 0.15 + 0.7 · hash(generation), y halfway
  between the top and the law's front (0.98 if the front is within 4% of the top). Burst = 1 is
  never. After the hole has crossed the frame's diagonal at u(4 µm) × Slow Motion, the frame is
  empty (black) for Refill seconds, then a new film (generation + 1).
- **Output alpha = mix(α, 1, Mix)**, the clip reflected premultiplied (as lithophane): a
  transparent part of the clip reflects nothing. At Mix 1 the film and its black are opaque.
- **Defaults.** Thickness 1.5 µm, Drainage 4 ×, Swirl 3 mm/s, Common, Burst 0.6, Slow Motion
  100 ×, Refill 1 s, Angle 0°: chosen (the law then bursts the film at 30.3 s of age, computed
  from the closed form). **Exposure 86.2515 from Resolume's 33 demo clips** (below).
- **CIE data under CC BY-SA 4.0** is compiled in and summed into the colour table, which is an
  adaptation: the tables and the colour table are CC BY-SA 4.0, the code MIT (ATTRIBUTIONS.md).
  **Settled for release (2026-11-18) by precedent:** filament (public, v0.1.0 on 2026-09-25)
  commits the same CIE 1931 dataset unedited (`tools/data/ciexyz31_1.csv`,
  doi:10.25039/CIE.DS.xvudnb9b) and bakes `source/PlanckTable.h` from it. Bubble ships its two
  CIE files unedited with the CIE's metadata, credits them with their licence and the CIE's
  copyright in ATTRIBUTIONS.md and on the website's attributions page, and says beside them
  (`data/README.md`) that they are the CIE's, under CC BY-SA 4.0, not MIT.
- **The harness skips the composite on frames it never reads** (`Rig::Advance`, Mix 0: the
  composite's early out). The plugin reads Mix only as the composite's uniform, so the clock,
  the burst's phases and the advection run exactly as at any Mix; the measured frame is always
  rendered at the check's own Mix.

## Changes from the spec's first draft

- **G table: 2 × 4096 indexed by v^{1/8}**, not 2 × 2048 by y^{1/4}. `--model` found the first
  table 2.4% off between its first nodes at the foot and 2e-5 near the top (once the harness's f
  stopped cancelling, below); the new one is at most 6.5e-6 between nodes for v ≥ 1e-12, and the
  checks use 1e-5 (`kShapeRelative`, the measured worst rounded up to a decade).
- **Exposure is 0 – 200, default 86.2515.** The first draft's 0 – 50 could not hold the rule's
  answer: a soap film reflects about 4% of the scene (the peak is 0.077 at 550 nm).
- **The stirring's speed bound is Σ|a_k| π √((p_k/W)² + (q_k/H)²)**, not the draft's
  Σ|a_k| π max(p_k/W, q_k/H). On review the draft's form is the tight bound for one mode (its u
  and v cannot peak at the same point), so the build's is merely looser: Swirl is never exceeded,
  but the flow's top speed is lower than it could be, by up to 1/√2 for one mode. Left as built
  (every measurement used it); changing back is one line in `FlowFor` and its restatement in the
  harness's `flowFor`.
- **Pugh's tolerances**: ±0.5 nm for the maximum (his integer 102), ±1 nm for the minimum (he
  gives 204, twice 102); measured 102.260 and 204.520 nm.
- **`--edges` is folded into `--drainage`**: the front is checked in every column, the edges
  included.
- **`--burst` at 12, 300 and 2000 nm** (Slow Motion 1000, 200, 100), every pixel against the
  disc, not "black film and 1 µm" along rows and columns.
- **`--black` checks the film over 30 nm too**: every row's colour against the harness's tint of
  the law's own thickness, not "the step is one pixel wide".
- **`--clock`** adds the stall, 5 s back and 10 s on (the frame's 0.1 s cap), and runs its seconds
  clock from 10 s, not 0 (a negative host time is no time: below).
- **`--rate`** compares at 1/12, 1/6 and 1/4 s; `--swirl` starts with one step held to float;
  `--refill` compares the new film with a fresh instance bit for bit rather than locating its
  front. **"The generation changes the nucleation point" is not checked** and the spec says so.
- `--resize` as built: 2W × 2H → W × H against W × H (bit-exact), then W × H → H × H.

## The traps actually hit

- **The harness's own f cancelled.** Written as 1 − (1 − e^{−(u/a)^b})^c it loses everything
  near f = 0 and f = 1; the G table's "node errors" of 0.0029 were the harness's. It is now
  `-expm1(c log1p(-exp(-(u/a)^b)))` with a complement form and its own bisection on each side.
- **Then the G table was wrong** (above): the warp mattered more than the node count.
- **A clock negative that the check could not see.** Reading the host's 4.99e8 ms in float is
  only wrong where the time is not a multiple of the float's 32 ms spacing; the first frames
  compared were at whole seconds. Frames 4, 10 and 17 of 60 fps now.
- **A negative host time is no time.** `UpdateClock` treats raw < 0 as "no host clock" and uses
  the wall clock. The clock check's "5 s back" from 0.28 s went negative; on the GPU the wall
  clock was still behind the film's age, so the interval clamped to 0 and it passed by luck. The
  software renderer, slower, put the wall clock ahead and the film moved. The seconds clock now
  starts at 10 s.
- **The software renderer is slow**: about 0.2 s a composite and 0.08 s an advection step at
  320 × 180 (single-threaded). The first `--all` on it was killed by its 900 s alarm inside
  `--drainage`. Fix: Mix-0 frames for what is not read, and shorter stirred runs (rate 180 steps,
  clock to frame 23, resize 18 frames, refill 5, robust 0.5 s and 2.4 s). It now takes about 11
  minutes, `--swirl` (2880 steps) and `--robust` (Swirl's own end, 1728 steps) most of it.
- **`robust`'s "burst at half way down" never happened**: at Thickness 0.2 µm, Drainage 10 the
  law puts it at 8.0 s, and the run was 3 s. Burst 0.1 (2.0 s) now, and the check counts the
  renders that end mid-burst (19 of 23).
- **The swirl band was too loose to see a 9% velocity error.** Mutating the sine's cubic term
  (1/6 → 1/7) passed `--swirl`: the band is an upper bound 23× the measured grid error. The
  one-step check (a zero field interpolates to exactly 0) holds the velocity itself to 4 ulp.
- **The GPU's cell position is not one value.** One step's displacement is off the double step by
  up to 2.62 ulp of the cell's height, where back.y − X.y should be within half an ulp: the
  position from `gl_FragCoord / GridSize` (a GPU division, 2.5 ulp in Metal's fast math) does not
  cancel. The tolerance is 4 ulp, stated.
- **An equivalent mutant**: the white's normalisation Σ S ȳ cancels in the white correction, so
  reading it with x̄ changes nothing. Replaced by the Z sum read with ȳ.
- **The refill rig's fresh copy** lacked Burst, Refill, Slow Motion and Drainage: a fresh
  instance must be given every control the compared one has (`cycling`).
- **stdout was block-buffered** when a run was killed by its alarm, so nothing showed where it
  died: `setvbuf(stdout, nullptr, _IOLBF, 0)`.
- From the brief, used: `std::max` drops NaN (`worse()` everywhere a worst is kept), no
  `timeout` (perl alarm), `half` is reserved in GLSL, no `||` in a mutant, delete the objects
  and the binary for each mutant.

## Verified, and assumed

Measured by `bbtest` on an Apple M4 Max, macOS 26.4.1, at 1280 × 720 and 320 × 180 on the GPU
and at 320 × 180 on Apple's software renderer (README's Status has every number):

- **Optics** (`--model`, `--colour`): IAPWS against its own Table 3 (3.1e-7, 5.8e-7 against
  1.6e-6); the plugin's n and Airy against the harness's own code and a 2 × 2 transfer matrix
  (2.2e-16, 6.8e-16); Pugh's 102/204 nm; the colour table at its nodes (5e-9) and between them
  (8.92e-5 against a bound of 1.4e-4 from its own second differences); 72 uniform films, every
  pixel, worst 0.675 of the tolerance.
- **The law** (`--black`, `--drainage`, `--model`): every row under 30 nm is the black film and
  every row over it the law's own colour (worst 0.254 of the tolerance); the front's row in
  every column exact at three settings and four ages; the G table (5.9e-8 at nodes, 6.48e-6
  between); the fit's residuals; the clock at 24/60/144 fps, 481 shared times, bit-exact.
- **Time** (`--rate`, `--clock`): bit-exact across frame rates, across a 4.99e8 ms clock, a stall,
  a jump back and a jump forward.
- **The stirring** (`--swirl`): one step within 4 ulp everywhere; after 4 s, 0 pixels beyond the
  band; the grid within 0.0015 of the traced displacement.
- **The burst** (`--burst`, `--refill`): the hole is the predicted disc to the pixel at 12, 300
  and 2000 nm; empty for exactly the span + Refill; the new film and New Film bit-exact against
  fresh instances.
- **Host** (`--resize`, `--alpha`, `--glstate`, `--robust`, `--pipe`): see README.
- **Exposure's default** on the demo clips, and white under transparent pixels on two of them.

**Chosen or assumed, not measured:**

- The stirring is a chosen flow, not Marangoni physics or marginal regeneration.
- f is a fit to points digitised from a figure (the digitisation's ±0.01 was not measured);
  the law was measured on fringes ≳ 100 nm and is extrapolated down to 30 nm; n and κ are one
  film's (Monier's Fig. 3), not the mean.
- h_cr = 30 nm; Reinold & Rücker's 12 nm known only through Ziapkoff et al.
- Taylor–Culick in black film is an extrapolation (McEntee & Mysels confirmed it above 0.1 µm);
  the hole's front is a straight-ray approximation of an eikonal.
- A flat film at one angle (no curvature); a per-channel tint of an RGB clip (no spectrum);
  unpolarised light; a black backdrop behind the film; out-of-gamut colours clipped.
- The defaults other than Exposure.

## Would this hold on another rasteriser, at another raster?

- **The software renderer** is another rasteriser with another float pipeline: every GL check
  passes on it at 320 × 180 with the same tolerances (verify.sh, every check, not a subset).
- **The bit-exact claims** (rate, clock, resize at one aspect, refill, New Film, robust's
  wild/tame) compare a renderer with itself, so they need only determinism, not any particular
  arithmetic; they would fail on a renderer whose result depends on scheduling.
- **The tolerance claims** are built from float roundings, the tables' own bounds and the
  encode, none fitted to this GPU. The one exception is the one-step check's 4 ulp, which assumes
  a GPU division within 2.5 ulp (Metal's fast math); a GPU with a worse division would fail it
  without the plugin being wrong.
- **Rasters**: 1280 × 720, 320 × 180, 2560 × 1440 and 640 × 360 (resize), 720 × 720 and
  180 × 180 (the aspect change). The grid does not depend on the raster, only on the aspect, so
  the grid's numbers (the one step, the field's error) are the same at both 16:9 rasters (they
  are). Portrait rasters and
  aspects other than 16:9 and 1:1 were not run; the swirl band was derived for any aspect but
  only measured at 16:9.

## The negative controls and mutants

`bbtest --negative`: 22 wrong models, each must make its check fail; all 22 are caught.

| check | wrong models |
| --- | --- |
| model | no half-wave phase change; no dispersion; s polarisation only; n = 1 in the law |
| colour | no half-wave phase change; no dispersion; s only |
| black | no transition (the law followed down to 0) |
| drainage | n = 1; t₀ ignored; a per-frame clock (a fixed 1/60 s a frame) |
| rate | one advection step a frame, of the frame's interval |
| clock | the host's time read in float |
| swirl | the flow's sign flipped; Swirl ignored |
| resize | a new raster restarts the film |
| burst | 1/u ∝ h, not √h (equal at 300 nm only) |
| refill | New Film ignored |
| alpha | the clip's alpha passed through; the clip's straight colour reflected |
| glstate | the host's GL state not put back |
| robust | the clip's NaN and range not tamed |

`tools/mutate.sh`: 65 one-character mutants of the shipped GLSL (27) and C++ (38), each naming
the check that must fail; 65 caught. Two survived the first run and are now caught by checks
added for them (the sine's cubic term by `--swirl`'s one step; the G table's row split at 0.6
by `--black`'s film rows, 36× its tolerance); one was equivalent and was replaced (above).

**Not covered by any mutant or check:**

- The burst's march under a **non-uniform** film: `--burst` forces a uniform film, so where
  the 48 samples fall along the segment does not change the picture. A mutant of the sample
  positions would pass.
- `kGridWidth`: the checks read the grid's size from the plugin, so a different width passes.
- The G table below its first node (v < 1e-12), reachable only by stirring film to the wall.
- That a new generation moves the nucleation point.

## The sweep's honest list

`tools/sweep.py`: all 12 controls change the picture in a context where they act (60 frames at
10 fps unless noted): Black Film at Drainage 10 (black film needs to exist), Slow Motion with
Burst 0 and Refill with Pop and Slow Motion 1 (5 frames: they act only once there is a burst),
New Film pressed at frame 30. `--bare` (no context, 60 frames) finds **Slow Motion and Refill
dead**: the default film does not burst until 30.3 s, beyond a 6 s sweep. That is the expected
answer, not a defect.

## The browser demo (2026-11-18)

https://bubble-demo.stoatworks-labs.com/, from `demo/`, deployed by `wrangler.toml` (a Worker
route: the zone is at Cloudflare's 100 custom domains, so `bubble-demo` needs a proxied AAAA
`100::` record, made through the API at the release, not by the demo's commits) and
`.github/workflows/deploy.yml` on every push to main (pinhole's, slug and host changed).

- **It runs the plugin, it doesn't port it.** `demo/bubble-core.wasm` is `Bubble.cpp` and
  everything it calls (Film, Controls, PassBuffer, Shaders, Diag) plus the FFGL SDK's plugin
  classes, FFGLShader and FFGLFBO, compiled unmodified by emscripten
  (`demo/tools/build-wasm.sh`, pinhole's). `CieTables.inc` is generated by the repo's own
  `bubble_cie_tables()` run in cmake's script mode (`cmake -P`), byte-identical to the
  configure-time file; the CSVs and the .cmake are hashed, the generated file is not kept.
  Left out: the registration file (`EffectPlugin.cpp`). The script fails if a `source/*.cpp`
  or a `data/*.csv` it doesn't list appears.
- **No threads.** Built without pthreads, emscripten's libc reports one core, so `Film.cpp`'s
  `hardware_concurrency() - 1` starts no helper and its own loop sums all 121 rows on the
  page's thread: the same table (each row is one thread's either way), 0.56–0.57 s in
  WebAssembly here against 0.029 s natively with helpers. No SharedArrayBuffer, so no COOP/COEP.
- **What is the page's**: `demo/wasm/glue.cpp` plays the host (constructs the plugin, reads its
  declarations back through the SDK so the panel is built from them, forwards
  SetFloatParameter, SetTime in seconds and ProcessOpenGL with Width equal to HardwareWidth and
  the canvas as the host FBO, as bbtest's Rig does, and hands the page the film's clock, the
  front, the law's mid-height thickness, the burst's drainage time and span, the steps run and
  the tint table's time); `demo/wasm/gl_shim.cpp` takes five GL calls (glShaderSource,
  refusing any text that isn't byte for byte the page's assembly of `demo/shaders.js`, then
  the kit's `port()`; glEnable/glDisable/glIsEnabled, for GL_PROGRAM_POINT_SIZE;
  glTexImage2D, below); the read-outs' units, the tooltips, Pop and New Film as press buttons
  (1 then 0), the line under the picture with its flash warning, Diag's log shown from
  emscripten's memory file system, and the presets (written in µm, ×, mm/s and degrees,
  converted by the plugin's own inverse functions through `bb_param_from`). The page's clock
  is the kit's (seconds from frame deltas, capped at 0.1 s, still while paused); the plugin
  votes on its unit against the wall clock, as in a host, and settles on seconds in a few
  frames.
- **Checked by `tools/verify.sh`** (`demo/tools/check_shaders.py`, before the build): every
  GLSL piece character for character, the order AssembleVertex()/Assemble() join them and
  InitGL's stage table; every uniform each pass declares set by the setter its GLSL type takes,
  arrays included, and nothing undeclared set (advect 7, composite 32); and the hashes of every
  input the .wasm was built from (`demo/wasm/inputs.sha256`, 37 files). Any change under
  `source/`, `data/` or `cmake/CieTables.cmake` needs `demo/tools/build-wasm.sh` rerun and its
  outputs committed.
- **Compared with bbtest, once (2026-11-18), by `demo/tools/crosscheck.py`** (in the repo;
  nothing runs it by itself: it needs Chrome, cdpshot and a built bbtest). Headless Chrome on
  the real GPU (ANGLE on Metal), Start pressed, the page paused; Mix 0 makes the composite hand
  the clip back untouched, so the canvas read then is the exact input frame (the colour bars,
  which stand still). For each case the page starts a fresh instance with the clock unit
  settled to seconds through `SetClockScaleForTest` before InitGL (`restartForTest`, a hook
  the page never calls), as the Rig does, and its host time is set to k/60 for frame k, one
  frame at a time; a press is 1 then 0 before its frame on the page and a `--script` cue (1 at
  the frame, 0 at the next) for `bbtest --pipe`, fed N + 2 copies of the frame with the same
  host values (the float32 the page sent). 320×180, Apple M4 Max, page through ANGLE/Metal
  against bbtest through Apple's OpenGL:

  | case | frame | colour values off (of 172,800) | max |
  | --- | --- | --- | --- |
  | defaults | 0 | 0 | 0 |
  | defaults, stirred | 30 | 1 | 1 code |
  | defaults, stirred | 60 | 1 | 1 code |
  | 0.6 µm, Drainage 10, Swirl 0 | 60 | 0 | 0 |
  | 0.6 µm, Drainage 10, Swirl 0 | 120 | 0 | 0 |
  | the same, Newton, Angle 45° | 120 | 0 | 0 |
  | Swirl 15 mm/s | 60 | 2 | 1 code |
  | Swirl 15 mm/s | 120 | 2 | 1 code |
  | 0.6 µm, Drainage 10, Pop at frame 20 | 21 | 0 | 0 |
  | the same | 30 | 0 | 0 |
  | New Film at frame 30 | 30 | 0 | 0 |
  | the same | 40 | 0 | 0 |
  | Mix 0.5, Exposure 30 | 30 | 0 | 0 |

  Alpha identical at every frame. The effect moves the clip by 10.0 to 85.1 codes on average.
  The negative controls on the unstirred 0.6 µm case at frame 120: bbtest one frame later puts
  40,549 values off (max 115 codes), Thickness one slider step (0.001) off puts 30,097 off (max
  24), against 0 for the case, so the comparison sees both the clock and a step the panel can
  make. The only differences are in stirred film after 360 to 1,440 advection steps; stirred
  runs of 10 to 30 frames (Pop, New Film, Mix) came out identical.
- **Also checked on the page** (headless Chrome, the real GPU, by content): before Start the
  page has no canvas and no panel, `#demo` holds only its `<noscript>`, in embed mode too;
  after Start the 12 controls in Film, Burst, Light and Look with the read-outs at the defaults
  (1.50 µm, 4.00 ×, 3.0 mm/s, Common, 0.60 down, 100 ×, 1.00 s, 0.0°, × 86.3, 1.00); the line
  under the picture at 3.01 s says 347 nm at mid-height and a burst in 27.3 s (30.3 s of age,
  the closed form's); the picture moves on its own; Thickness down plus New Film makes
  generation 2 and changes the picture; Pop bursts it; Burst 0, Refill 0 and Slow Motion 1 show
  the flash warning and the defaults clear it; all seven presets render; no horizontal scroll
  at 375 px after shrinking; no console errors and no WebGL warnings (CDP's Runtime and Log).
- **The photosensitivity gate** (xenon's): nothing is mounted until Start, and the warning
  says which settings flash, from the release's measurement over Resolume's demo clips, not
  this page's: Burst 0 with Refill under 0.1 s and Slow Motion below its default (up to 11
  flashes a second in part of the frame), Pop pressed twice a second or faster with a short
  Refill (up to 6.5), and the defaults on clips that already flicker (2 of 33, Exposure
  brightening them). The line under the picture warns when a film is pulled and burst faster
  than three times a second (its age at the burst, the burst's span and Refill, from Film.h's
  functions); it cannot see Pop pressed by hand or a flickering clip of your own.
- **Every gap, as the page discloses it** (banner, the "does not reproduce" list): it runs the
  plugin, not a port, and the panel's units, tooltips and presets are the page's; GL is
  emscripten's WebGL2 with five shimmed entry points and the kit's GLSL port (version line,
  highp defaults), GL_PROGRAM_POINT_SIZE reading as off; the R32F grid needs
  EXT_color_buffer_float (the page says so and doesn't start without it) and the arithmetic is
  the browser's GPU, not the Mac's driver; no threads, no fused multiply-add, musl's libm; the
  host clock is the page's, Restart is a jump back (no time passes), a frame is a display
  refresh; the raster is the page's; the input is unpadded with the viewport at the origin and
  the output an 8-bit canvas; the kit uploads premultiplied colour where the plugin expects
  straight, so the kit's alpha clip is left out; the crosscheck ran once, on one GPU; Pop and
  New Film are buttons and the About block is off the panel; nothing is measured, and the
  plugin has never been in Resolume.
- **Traps hit**: the SDK's FFGLFBO allocates the grid's R32F buffers as
  `glTexImage2D(..., GL_R32F, ..., GL_RGBA, GL_FLOAT, NULL)`, which desktop GL takes and
  WebGL2 refuses (INVALID_OPERATION: ES pairs R32F with GL_RED only), leaving the framebuffer
  incomplete; the shim gives that allocation GL_RED and forwards the two table uploads to
  emscripten's own `emscripten_glTexImage2D` unchanged. The clock vote means a crosscheck must
  settle the unit before InitGL, or the first frames run on the wall clock and no two runs
  agree. A page frame counter that restarts with the instance hangs a script waiting for the
  next frame; it counts across instances. The kit's hidden embed statement says "ported to
  WebGL2" even for a WebAssembly demo (kit text, left alone).

## In Resolume (2026-11-18, the release)

- **macOS, Arena 7.29.0 on the M4 Max**, the CI's universal bundle from 50fc8d4 (the plugin's
  source is unchanged to the tag), driven over Arena's REST API on 8080 and read off the
  composition snapshot, over a still frame of IntoTheGlow_21 (the release scratch's
  `mac/macprobe.py`). All 17 parameters (18 with Opacity) at their declared defaults; Arena's
  clock in ms, voted (scale 0.001); Mix 0 the bare still exactly. The film moves on a still, so
  every control was read in a calm film (Drainage 0.1, Swirl 0, Burst 1) against a
  same-settings pair (5.0 levels over 1.5 s): Thickness 0.2 um 38.0 levels live (4 um only
  5.9: thick films are all pale), Angle 60 42.2, Exposure 0 96.9 / 200 38.7, Swirl 20 mm/s
  12.3, Drainage 10 37.5, Black Film Newton 3.8 on a held black-topped film (floor 0.41). Pop
  and New Film pressed over REST (PUT true, then false): a Pop at 1000x still growing at 3 s;
  New Film within 0.18 levels of a fresh film. **The defaults' timeline on Arena's clock**: a
  new film whole at 30.0 s, gone by 30.3 s, black to 32.3 s, a new film at 32.5 s (the law's
  30.29 s, the 1.22 s span, the 1 s Refill). A clip retrigger keeps the film (same effect id).
  The Quit! dialog came up; Quit (never Save & Quit), show files' dates unchanged.
- **Windows, the win-lab gate** (plugin-bench `arena/expect/bubble.json`): Arena 7.27.1, llvmpipe, the
  dispatch DLL from 50fc8d4: 9 passed, 0 failed, 1 skipped (Pop and New Film inert). The film
  drains between grabs, so the noise floor at the defaults is 40.2 levels; the drifting
  controls passed on the between/within spread ratio (Thickness 7.2, Mix 5.7, Swirl 4.9, Black
  Film 3.2 at Drainage 1.0, Angle 3.2, Opacity 2.3, Drainage 1.9), the rest outright (Exposure
  101, Burst 101, Slow Motion 63 at Burst 0, Refill 25 at Burst 0 + Slow Motion 0). Its log:
  llvmpipe, `initialised`, clock scale 0.001.
- **Found while checking the video's captions**: at the defaults the film is so thin by 30 s
  that even at 100x the hole crosses in 4 frames; the frame then waits out the rest of the
  burst's span (sized for 4 um film) and the Refill, about 2 s of black. The video's caption,
  the guide and the README say so; whether the empty time should start when the hole has
  crossed is open question 10.

## Open questions

1. **CC BY-SA 4.0** (settled for v0.1.0, above). The CIE tables are compiled into the binary
   and summed into the colour table (an adaptation): "data and colour table CC BY-SA, code MIT",
   as filament's release reads. An unencumbered fit (Wyman–Sloan–Shirley, as holofoil) remains
   possible later; it would change every colour, so it would be a new release.
2. **The defaults.** At 1.5 µm and Drainage 4 the black film is 1% of the way down at 4.6 s and
   9% at 10 s, and the film bursts at 30.3 s (the closed form); the bottom stays high-order grey for much of it. A thinner or
   faster default would show more colour sooner. Your call.
3. **Exposure 86.** By the fleet's rule; on the brightest demo clips the film then clips hard
   (IntoTheGlow_02's p99 at Exposure 1 is 0.072, so 6.2 at the default). The range 0 – 200 is
   unusual for the fleet.
4. **A transparent backdrop?** At Mix 1 the empty frame and the dark film are opaque black, and
   Add or Screen lay it over lower layers. Should the film carry its own alpha instead?
5. **Curvature.** The name says bubble; the model is a flat film at one angle. A sphere's angle
   runs 0 → 90° from centre to rim (the table stops at 60°).
6. **The swirl band** is an upper bound, 23× the measured grid error after 4 s; a per-path
   estimate would be tighter. With the one-step check holding the velocity to float, is the
   bound good enough as it is?
7. **Slow Motion 100** by default: real time puts the whole burst inside one frame at 60 fps.
8. **Resolume's alpha**: the plugin treats the clip's colour as straight; whether Resolume hands
   an effect premultiplied colour has not been checked.
9. **The stirring's speed bound**: the build's root-sum-square form leaves the flow up to
   1/√2 slower than Swirl says; the draft's max form is tight per mode. Change it back?
10. **The burst's empty time.** The burst phase lasts the time the hole would take through 4 um
    film (1.22 s at 100x, 12.2 s at 1000x for 16:9) however quickly the real hole crosses, and
    Refill starts only after it. At the defaults the hole is across in 4 frames, then ~2 s of
    black; at 1000x a burst is followed by ~12 s of black before Refill. End the phase when the
    hole has crossed the frame instead? (Released as built.)
