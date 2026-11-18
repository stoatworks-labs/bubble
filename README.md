# bubble

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. **It has never been loaded into
> Resolume.** What's established is established by an offline harness, `bbtest`, that drives
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

**v0.1.0, being released, 18 November 2026. Not yet loaded into Resolume.**

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

- **No host has loaded it.** Not Arena, not Avenue, on any platform. `oxbow` (the fleet's test
  host) reads the bundle and renders through it, which is all.
- **Windows** is built by CI (`.github/workflows/ci.yml`, x64, GLEW from vcpkg) and the
  release's DLL is checked by `tools/wincheck.ps1`, which loads it as a host does; none of the
  pixel checks has run on Windows. The x86_64 macOS slice is built and checked with `lipo`; it
  has never run.
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
- No user guide, no presets, no OpenFX port and no browser demo.

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
