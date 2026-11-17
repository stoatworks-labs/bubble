# Attributions

Bubble is built on other people's work. This file lists what that work is, who did it, and
what it is doing here.

**A provisional hand copy (2026-11-17).** Bubble is not yet registered in
`stoatworks-backend`, so this was written by hand in the generated file's shape. Once it is
registered, `scripts/sync-attributions.py` generates it from the master lists; edit it there
then, not here.

## Data we ship

### CIE 1931 colour-matching functions, 2° observer — CIE

`data/CIE_xyz_1931_2deg.csv` (360–830 nm, 1 nm), with the CIE's own metadata beside it
(`data/CIE_xyz_1931_2deg.csv_metadata.json`), unchanged.  
Source: CIE 018:2019 Table 6; ISO/CIE 11664-1:2019. CIE dataset
<https://doi.org/10.25039/CIE.DS.xvudnb9b>, downloaded from files.cie.co.at on 2026-11-16.  
sha256 `fa663e3535a7e0763a745993a1f0a192eb0275ac46ad2d1befd7626841e713c1` (matches the
metadata's checksum).  
Licence: Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)  
Copyright: International Commission on Illumination (CIE)

### CIE standard illuminant D65 — CIE

`data/CIE_std_illum_D65.csv` (300–830 nm, 1 nm) and
`data/CIE_std_illum_D65.csv_metadata.json`, unchanged.  
Source: ISO/CIE 11664-2:2022 Table B.1. CIE dataset
<https://doi.org/10.25039/CIE.DS.hjfjmt59>, downloaded from files.cie.co.at on 2026-11-16.  
sha256 `e76f210bffff3d552ef7113025da5f325d5dfec200dd4b878b1a2f3a507032cb` (matches the
metadata's checksum).  
Licence: Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)  
Copyright: International Commission on Illumination (CIE)

**What the licence means here.** Both tables are compiled into the plugin (CMake turns the
CSVs into a source file) and summed into its colour table, which is an adaptation of the
data. CC BY-SA asks that the data and adaptations of it be credited (this file and the About
text) and shared under the same licence: the tables and the colour table derived from them
are CC BY-SA 4.0; the code is MIT. Whether that is how Allan wants a release to read is an
open question in AGENTS.md.

## Code we derived from other people's work

### The harness and the tooling — Stoatworks lithophane, convergence, and ferrofluid before them

<https://github.com/stoatworks-labs/ferrofluid>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's rig, PNG writer, cue sheets, `--set` by display name, `--pipe`/`--film`
(SIGPIPE ignored, a closed stdout an exit of 1), `--negative` with its Perturb, and
`--offline` (tools/bbtest is lithophane's lptest, which is convergence's cvtest, intensifier's
ittest and ferrofluid's fetest, under new checks); tools/verify.sh, tools/mutate.sh (with
fountain's perl alarm), tools/glslc.sh, tools/sweep.py and tools/clips.py. All adapted.
lithophane and convergence live in ~/dev and are not published yet.

### The clock — Stoatworks nipkow, ferrofluid and fountain

The host clock's seconds-or-milliseconds vote, the wall-clock fallback and the integer
microseconds (source/Bubble.cpp, `UpdateClock`) are nipkow's, which are ferrofluid's vote and
fountain's offline fallback.

### The pass buffers, the GL state and the log — Stoatworks millpond and radar

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} and source/Diag.{h,cpp} are millpond's, by way of intensifier,
convergence and lithophane. source/GLState.h is radar's, which is vectrix's, from
resolume-scopes, with convergence's additions.

### The About block — Stoatworks zoetrope

<https://github.com/stoatworks-labs/zoetrope>  
Licence: MIT  
Copyright: Stoatworks Labs

source/StoatworksAbout*.h, a provisional hand copy of lithophane's, itself from zoetrope's
first header (no user guide yet).

### The FFGL SDK — Resolume

<https://github.com/resolume/ffgl> (submodule at `external/ffgl`, pinned at `b1afaf9`)  
Licence: BSD-style (see `external/ffgl/LICENSE.md`)  
Copyright: Resolume, and the FreeFrame contributors

## Published work this is based on

Each is cited where it is used (SPEC-bubble.md, the source comments, the harness's output).

- S. J. Byrnes, "Multilayer optical calculations", arXiv:1603.02720 — the Fresnel
  coefficients and the transfer matrix.
- IAPWS R9-97, the refractive index of ordinary water substance; NIST Chemistry WebBook
  (SRD 69) for the density of water at 20 °C.
- IEC 61966-2-1 — the sRGB matrix and transfer function.
- L. Monier et al., "Self-similar and Universal Dynamics in Drainage of Mobile Soap Films",
  arXiv:2401.03931 — the drainage law and its master curve (digitised from their Fig. 3b).
- R. J. Pugh, *Bubble and Foam Chemistry* (CUP 2016), ch. 3 — black films.
- V. Ziapkoff et al., arXiv:2511.21383 — black-film thicknesses' history.
- A. Savva, J. W. M. Bush, J. Fluid Mech. 626 (2009) — the Taylor–Culick speed.
