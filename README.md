# bubble

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. It has been loaded into Resolume
> Arena on macOS and on Windows and ran there, over still pictures (see [Status](#status)).
> Everything else below is established by an offline harness, `bbtest`, that drives
> the real plugin class through the same `ProcessOpenGL` a host calls, in a headless GL
> context, on this Mac's GPU at two rasters and on Apple's software renderer. Read off the
> picture, a uniform film's colour matches the harness's own **transfer-matrix × CIE 1931 ×
> D65** sum to within **0.675** of a tolerance built from the colour table's own bound; the
> black front of a draining film sits **on the row Monier et al.'s drainage law predicts, in
> every column**, at three settings and four ages; the film is **bit-identical at 24, 60 and
> 144 fps** and across Resolume's 4.99 × 10⁸ ms clock; the burst's hole is the **Taylor–Culick
> disc to the pixel**. **22** deliberately wrong models and **65** one-character mutants of the
> shipped code are all caught. The stirring, the master curve's fit and every default but
> Exposure are **chosen**, not measured: see [Status](#status).

The clip reflected in a draining soap film, for [Resolume](https://resolume.com) Arena and
Avenue: an FFGL effect, **SW Bubble**.

A soap film is a sheet of water a few hundred nanometres thick. Light reflected from its front
face and its back face interferes, so how much of each colour comes back depends on the
thickness. Gravity drains the film, so it thins from the top: bands of Newton's colours slide
down and spread, the top goes silvery white and then, below about 30 nm, black, with a sharp
edge. Stir it and the colours swirl. Then it bursts: a hole opens in the black film and races
out, faster through thin film than thick. The clip is what the film reflects, tinted by it.
Nothing is drawn: it all comes out of the thickness.

**Photosensitivity.** Bubble makes flashes of its own: a burst takes the picture to black and a
new film brings it back. At its defaults it bursts once every half minute or so, but measured
with the WCAG general-flash rule (more than three flashes a second over the whole frame or any
third-of-frame window) over Resolume's 33 demo clips, it goes over the line in three ways:
**Burst at 0 with a Refill under 0.1 s** at Slow Motion 10 × or less, which bursts each film as
it is made (**14** of the 33 clips, up to 11 flashes a second in part of the frame); **Pop
pressed twice a second or faster** with Slow Motion at 1 × and a short Refill (1 clip at twice a
second, 11 at three times); and **at the defaults, on 2 clips that already flicker**, because
Exposure brightens what the film reflects. The defaults are as designed; the
[user guide](docs/USER-GUIDE.md#flashing) has the numbers and what keeps it under the line.

## Controls

| Group | |
| --- | --- |
| **Film** | Thickness (0.2–4 µm at mid-height when the film is pulled), Drainage (0.1–10 × the real 25 mm film's rate), Swirl (0–20 mm/s, the bound on the stirring's speed), Black Film (Common, 12 nm; Newton, 4 nm). |
| **Burst** | Burst (how far down the black film comes before the film bursts; 1 is never), Slow Motion (1–1000 ×), Refill (0–10 s of empty frame before a new film), Pop (burst now), New Film (a fresh film now). |
| **Light** | Angle (0–60°, the angle of incidence), Exposure (0–200, linear light). |
| **Look** | Mix. |

The defaults are a 1.5 µm film draining at 4 × the real rate, stirred at up to 3 mm/s, common
black film, bursting when the black has come 0.6 of the way down (30 s into its life), shown at
100 × slow motion, refilled after 1 s, seen face on. Exposure's default, **86.2515**, was set on
Resolume's own 33 demo clips: there, the brightest 1% of the median clip just reaches full
scale in its brightest channel. A soap film reflects about 4% of the scene, so it is large.

**The film is flat and seen at one angle**: a real bubble's angle runs from 0 at its centre to
90° at its rim. **The swirls are a chosen stirring**, six smooth modes, not a simulation of the
film's own flow. **The film reflects a black room**: a transparent part of the clip reflects
nothing, the empty frame after a burst is black, and the output is opaque at Mix 1 (Mix blends
premultiplied). Use Add or Screen to lay it over other layers.

## Status

**v0.1.0, 18 November 2026, and early days.** It has been loaded into Resolume Arena on
Windows and on macOS (below), over still pictures: never a moving clip, never in a show. The
macOS build is Developer ID-signed and notarised. There's a [user guide](docs/USER-GUIDE.md)
and a [browser demo](#browser-demo), but no OpenFX port and no presets.

### In Resolume

![A tunnel of metal struts seen in a soap film, bands of colour across its top, with a black
hole opening from the top edge: wide along the top, curving down to a point in the middle of
the picture](docs/arena.jpg)

<sub>SW Bubble inside Resolume Arena 7.29.0 on an Apple M4 Max, from Arena's composition
snapshot over REST, 18 November 2026: a frame of the demo clip IntoTheGlow_21 held still, a
fresh 1.5 µm film at Drainage 0.1 ×, one second after Pop was pressed over REST at Slow Motion
1000 ×. The hole opened near the top, where a new film is thinnest, and raced along the thin
film there faster than down into the thick film below.</sub>

- **Windows: Arena 7.27.1 on win-lab, software rendering (Mesa llvmpipe, no GPU)**, the
  release workflow's x64 DLL from a dispatched build of 50fc8d4 (the plugin's source is
  unchanged from there to the tag), through the fleet's Arena gate on 18 November 2026:
  **9 passed, 0 failed, 1 skipped**. Arena loaded it and registered `SW Bubble` `BB01` as an
  effect; all 17 parameters (with Arena's Opacity, 18) matched name, order, type, range and
  default; the frame had content, 37.5 levels from the bare input at the defaults (the film
  drains between grabs, so the noise floor is 40.2 levels); and every control it can set moved
  the picture, Black Film at Drainage 10 ×, Slow Motion at Burst 0 and Refill at Burst 0 and
  Slow Motion 1 ×, where they act. Exposure, Burst, Slow Motion and Refill moved it outright
  (101, 101, 63 and 25 levels); the controls that change a film that is already changing
  passed on the ratio of the spread between settings to the spread within one (Thickness 7.2,
  Mix 5.7, Swirl 4.9, Black Film 3.2, Angle 3.2, Opacity 2.3, Drainage 1.9). The skip is Pop
  and New Film, recorded inert: the gate sends no presses. No shader or error lines in
  Arena's log, and Arena stayed up. The plugin's own log: llvmpipe (Mesa 26.2),
  `initialised`, and Arena's clock in milliseconds, detected (scale 0.001).
- **macOS: Arena 7.29.0 on an Apple M4 Max**, the release workflow's universal bundle from a
  dispatched build of 50fc8d4 (ad-hoc signed, not the notarised download; the plugin's source
  is unchanged from there to the tag), mounted on a clip
  over Arena's REST API and read off Arena's composition snapshot (the mean difference over the
  middle three fifths of a 1920×1080 JPEG), over a frame of IntoTheGlow_21 held still. Arena's
  log: `registered extension: 'SW Bubble' uid: BB01 category: 1`.
  - all **17** parameters shown (18 with Arena's own Opacity) at the declared defaults, Black
    Film offering Common and Newton, Pop and New Film as events, and the About line
    `Bubble v0.1.0 - MIT - Stoatworks Labs, stoatworks-labs.com`. Its log: Arena's clock arriving
    in milliseconds, which it detected (scale 0.001);
  - Mix 0 gives the bare still exactly (**0.0**); at the defaults two snapshots a second apart
    differ by **45.8** levels (the film draining on Arena's clock);
  - in a calm film (Drainage 0.1 ×, Swirl 0, Burst never), two snapshots 1.5 s apart differ by
    **5.0** levels, and against that: Thickness 0.2 µm **38.0** levels (live, no New Film; 4 µm
    only 5.9, as thick films are all pale), Angle 60° **42.2**, Exposure 0 **96.9** (black) and
    200 **38.7**, Swirl 20 mm/s for 2 s **12.3** (still again once it stops, 5.4), Drainage 10 ×
    for 2 s **37.5**;
  - Black Film: on a black-topped 0.2 µm film held still (floor **0.41**), Newton against Common
    **3.8** levels, the black top's mean falling from 14.5 to 2.5; back to Common, **0.57**;
  - **Pop**, pressed over REST at Slow Motion 1000 ×: **25.4** levels after 1 s, and the hole
    still growing, **71.6** more by 3 s (pictured). **New Film** during that burst: back to a
    fresh film, **0.18** levels from one pulled earlier. Burst 0 at 1000 × burst the film as soon
    as it was set (**36.9** levels 1.5 s later). Slow Motion 1 × with Refill 10 s: the frame
    black 2 s after a Pop; with Refill 1 s, a new film there instead (**1.7** levels from before
    the Pop);
  - **the defaults on Arena's clock**: a new film pulled over REST was whole at 30.0 s and gone
    by **30.3 s**, black until **32.3 s** and a new film at **32.5 s**: the law's burst at 30.29 s,
    the burst's 1.22 s span and the 1 s Refill (the harness shows the same: the film gone in 4
    frames, even at 100 ×);
  - a clip retrigger doesn't pull a new film: 0.5 s after it the film was **8.3** levels from just
    before and **30.8** from a fresh one, the same effect instance throughout;
  - nothing in Arena's log beyond the load and registration, and Arena stayed up. It quit
    without saving (the Quit! dialog's Quit; every show file's date unchanged) and the bundle
    was taken out again.

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple M4 Max, macOS 26.4.1) against a fresh universal
Release build (17 November 2026). Every picture check runs at **two rasters**, 1280×720 and
320×180, and again on **Apple's software renderer** at 320×180. The numbers below are from
`bbtest --all` on the same commit (an arm64 build, the same code); 320×180 in brackets where
it differs.

| check | result |
| --- | --- |
| `--model` | IAPWS R9-97 against its own Table 3: 226.5 and 1013.98 nm off by **3.1e-7** and **5.8e-7** (tolerance 1.6e-6, the release's rule); the plugin's n(λ) and Airy against the harness's own code and a 2×2 transfer matrix to **2.2e-16** and **6.8e-16**; at 546 nm the last maximum and minimum at **102.260** and **204.520 nm** (Pugh: 102 and 204); the colour table 5e-9 at its nodes and **8.92e-5** between them (bound 1.4e-4 from its own second differences); the G table **5.9e-8** at its nodes and **6.48e-6** between them, relative; the master curve's fit rms **0.0090**, worst 0.0173; the film's clock at 24, 60 and 144 fps over 40 s of bursts and refills, **0** of 481 shared times different |
| `--colour` | 72 uniform films (18 thicknesses, 0–59.75°) over white, every pixel against Exposure × the harness's tint: worst **0.675** of the tolerance |
| `--black` | a draining film with its front 0.55 up: all **325** rows (81) under 30 nm are the 12 nm (Common) or 4 nm (Newton, at 40°) film, worst 0.209 of the tolerance; all **395** rows (99) over it are the harness's tint of the law's own thickness, worst **0.254** |
| `--drainage` | the black front in every column against the law at fronts 0.95, 0.75, 0.5 and 0.3, at 1.5 µm and 3 µm (Drainage 4) and 1.5 µm (Drainage 10): **0** column-measurements off the law's row, **0** with black below the front |
| `--rate` | 24 and 144 fps against 60 at 1/12, 1/6 and 1/4 s, stirred: **0** values differ; 180 advection steps by 1/4 s |
| `--clock` | a host clock in ms from 4.99e8 against seconds from 10: **0** values differ; 30 frames at one host time, 5 s back, then 10 s on (the film runs the frame's cap, 0.1 s): **0** differ |
| `--swirl` | one step from a still film, all 512×288 grid cells against the harness's double RK2 step: worst **2.62 ulp** (tolerance 4 ulp); after 4 s at Swirl 10 mm/s, 5184 pixels traced back in double: the flow carried **149** (146) across the front, **0** disagree beyond the band 0.0357; the grid within **0.0015** of the traced displacement |
| `--resize` | 2560×1440 for 12 frames then 1280×720 for 6, against 1280×720 throughout, stirred: **0** values differ; to 720×720: the grid resampled within **3.7e-9** of the harness's bilinear, **0** of 518,400 pixels' black film misplaced |
| `--burst` | Pop in a uniform film of 12, 300 and 2000 nm (u = 76.44, 15.29, 5.92 m/s): **0** pixels off the predicted disc beyond float, at two instants each |
| `--refill` | New Film after 0.5 s of stirred film against a fresh instance: **0** values differ; the cycle: burst at 4.095380 s, 5 empty frames all black, the new film at 4.607559 s against a fresh instance at its age: **0** differ |
| `--alpha` | alpha = mix(α, 1, Mix), 0 off at Mix 1 and 4.5e-8 at Mix 0.5; white under α = 0 changes **0** values; Mix 0 the clip bit for bit. On two of Arena's own alpha clips, white painted under every transparent pixel changes **0** output bytes |
| `--glstate` | the host's GL state comes back, **0** things changed, advection passes included |
| `--robust` | a clip with −1, 2, 10³⁰, ±inf and NaN renders exactly as its tamed copy; 23 renders with a control at an end (19 ending mid-burst), **0** non-finite; Mix 0 hands the clip through byte for byte |
| `--pipe` | 2.5 frames in, exactly 2 out; a bad cue, a bad option, `abc`, `nan`, `inf`, an empty value and `0.5x` refused (exit 2); an option by name taken; a closed stdout (`\| head -c 1`) exits 1, not 141 |
| `--negative` | **22** deliberately wrong models, **22** caught |
| mutation | **65** one-character mutants of the shipped GLSL and C++, **65** caught |
| `tools/sweep.py` | all **12** controls measurably change the picture |
| shaders | all 3 compile through `glslc`, not merely through Apple's driver |
| the bundle | universal (`x86_64 arm64`), exports `plugMain`, ad-hoc signs; `oxbow` reads `SW Bubble` / `BB01` / `effect` and renders through it |

Render cost, from verify.sh's final run on 17 November 2026 on a shared machine (other builds
running), over the harness's card, GPU time (`GL_TIME_ELAPSED`): with the film still,
**0.25 ms** a frame at 720p (worst 0.81) and **0.61 ms** at 1080p (worst 0.74); stirred at the
default 3 mm/s (12 advection steps a frame at 60 fps), **0.76 ms** at 720p (worst 0.83),
**1.31 ms** at 1080p (worst 1.56) and **4.17 ms** at 4K (one run; worst 4.36). An earlier run
the same day gave 0.44 and 0.79 ms still: the still frame's cost moves with the machine's load. The burst's 48-tap march (only while bursting) and the
CPU's time a frame were not measured separately. macOS only.

### Not established

- **Resolume, beyond a still picture.** It has loaded in Arena on macOS and Windows (above),
  never on a moving clip, never in a show, never in Avenue.
- **Windows** is built by CI (`.github/workflows/ci.yml`, x64, GLEW from vcpkg) and the
  release's DLL is checked by `tools/wincheck.ps1`, which loads it as a host does, and by the
  Arena gate (above); none of the harness's pixel checks has run on Windows. The x86_64 macOS
  slice is built and checked with `lipo`; it has never run.
- **No real soap film has been compared with it.** Every check is against the published
  physics and the harness's own restatement of it, not against a filmed bubble.
- **Chosen, not measured:** the stirring (six box modes, not the film's own Marangoni flow);
  the master curve f (a fit to ten points digitised from Monier et al.'s figure); the law's use
  below 100 nm, where it was not measured; the 30 nm black-film threshold; every default but
  Exposure.
- **The burst** is tested only in a uniform film: through a film of varying thickness the hole's
  front is a straight-ray approximation, and the march's sample positions are not checked.
- **Resolume's alpha.** The plugin treats a clip's colour as straight alpha, which is what
  Arena's DXV demo clips decode to; whether Resolume hands an effect premultiplied colour has
  not been checked.
- **The burst's empty time.** After a burst the frame waits out the burst's whole span, the
  time the hole would take through 4 µm film (1.22 s at 100 ×, 12.2 s at 1000 × for 16:9),
  however quickly the hole really crossed: at the defaults the film has thinned for 30 s and is
  gone in 4 frames, then the frame is black for about 2 s. Whether the empty time should start
  when the hole has crossed is open.
- **Open questions** (AGENTS.md has the detail): whether a thinner or faster default film,
  showing more colour sooner, would be better; Exposure 86 clipping the brightest clips hard
  (IntoTheGlow_02 at about six times full scale), and its unusual 0–200 range; whether the
  film should carry its own transparency rather than an opaque black backdrop; a curved-bubble
  mode (the angle running 0 to 90° from centre to rim, where the table stops at 60°); the
  stirring's speed bound, which leaves the flow up to 1/√2 slower than Swirl says; and the
  stirring itself, chosen rather than the film's own Marangoni flow.
- No presets and no OpenFX port.

## Browser demo

[bubble-demo.stoatworks-labs.com](https://bubble-demo.stoatworks-labs.com/) runs the plugin
in your browser: its own C++, the plugin class with its film's clock and its colour table
summed from the CIE's data, compiled unmodified to WebAssembly and drawing with its own GLSL in
WebGL2. The page is the host and its clock, the clips are generated (or a picture or video of
your own) and the raster is the page's. Nothing moves until you press Start under its
photosensitivity warning: some settings flash. Stepped frame by frame, the page matched
`bbtest --pipe`'s output on the same input, settings and host clock at 13 frames in eight
cases: nine byte for byte, the four longer stirred ones by one 8-bit code in 1 or 2 of
172,800 values. It needs WebGL2's EXT_color_buffer_float. It is a demo, not the plugin, and
the page lists what it leaves out.

## Build

macOS (universal: Apple Silicon and Intel):

```bash
git clone --recursive <this repo>
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Bubble.bundle` into `~/Documents/Resolume Arena/Extra Effects`. Restart
Arena and it's under Effects as **SW Bubble**.

## Building and testing

```bash
OXBOW=/path/to/oxbow tools/verify.sh
```

That's the whole lot: shader hygiene and `glslc`, a fresh universal build with no warnings,
the bundle's architecture, plist and ad-hoc signature, a host's view of it through `oxbow`
(required), every check at two rasters and again on the software renderer, the alpha check and
Exposure's default on Arena's demo clips (required), the pipe, the negative controls, the
mutants, the sweep for dead controls and the render cost. CI (`.github/workflows/ci.yml`)
builds on macOS and Windows and runs the checks that need no GL; this script is the
verification. `CLAUDE.md` lists the checks one by one; `./build/bbtest --all` runs them all.

Render a frame of your own, ten seconds into the film's life:

```bash
./build/bbtest --out /tmp/bubble.png --frames 600 --set "Black Film=Newton" --set "Swirl=0.5"
```

(An option takes its name; a slider takes the host's 0..1, so Swirl 0.5 is 10 mm/s.)

Run a clip through it:

```bash
ffmpeg -nostdin -i in.mov -f rawvideo -pix_fmt rgba - | ./build/bbtest --pipe --size 1280x720 | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mov
```

## License

The code is MIT: see [LICENSE](LICENSE). The CIE colour-matching functions and illuminant D65
under `data/` are the CIE's, © International Commission on Illumination, and they and the
colour table the plugin sums from them are CC BY-SA 4.0, not MIT: see
[data/README.md](data/README.md) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
