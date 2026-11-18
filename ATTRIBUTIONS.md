# Attributions

Bubble is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The harness and the tooling — Stoatworks lithophane, convergence, and ferrofluid before them

<https://github.com/stoatworks-labs/lithophane>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's rig, PNG writer, cue sheets, --set by display name, --pipe/--film (SIGPIPE ignored, a closed stdout an exit of 1), --negative with its Perturb, and --offline (tools/bbtest is lithophane's lptest, which is convergence's cvtest, intensifier's ittest and ferrofluid's fetest, under new checks); tools/verify.sh, tools/mutate.sh (with fountain's perl alarm), tools/glslc.sh, tools/sweep.py and tools/clips.py. All adapted.

### The clock — Stoatworks nipkow, ferrofluid and fountain

<https://github.com/stoatworks-labs/nipkow>  
Licence: MIT  
Copyright: Stoatworks Labs

The host clock's seconds-or-milliseconds vote, the wall-clock fallback and the integer microseconds (source/Bubble.cpp, UpdateClock) are nipkow's, which are ferrofluid's vote and fountain's offline fallback.

### The pass buffers, the GL state and the log — Stoatworks millpond and radar

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} and source/Diag.{h,cpp} are millpond's, by way of intensifier, convergence and lithophane. source/GLState.h is radar's, which is vectrix's, from resolume-scopes, with convergence's additions.

### The release workflow and the DLL check — Stoatworks convergence, intensifier, xenon and penumbra

<https://github.com/stoatworks-labs/convergence>  
Licence: MIT  
Copyright: Stoatworks Labs

.github/workflows/ci.yml and release.yml are convergence's, which are intensifier's and xenon's (slitscan's before them): a universal bundle and a Windows DLL, ATTRIBUTIONS.md in each zip, the Windows installer per user into Resolume's Extra Effects. tools/wincheck.ps1, which loads the DLL as a host loads it and reads back its name, id, type and parameters, is penumbra's (pepper's). Adapted.

### The look card — Stoatworks schlieren

<https://github.com/stoatworks-labs/schlieren>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's test card (a sunset sky, a sun, a skyline, colour bars), the default clip in bbtest and the picture its checks reflect.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Data this project ships

Measurements made by other people and carried inside the product. Each is credited the way its publisher asks, and where a licence requires its attribution to travel with the data, that text is reproduced here.

### CIE 1931 colour-matching functions, 2° observer — CIE

<https://doi.org/10.25039/CIE.DS.xvudnb9b>  
Licence: Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)  
Copyright: International Commission on Illumination (CIE)

data/CIE_xyz_1931_2deg.csv (360–830 nm, 1 nm), with the CIE's own metadata beside it (data/CIE_xyz_1931_2deg.csv_metadata.json), unchanged. Source: CIE 018:2019 Table 6; ISO/CIE 11664-1:2019; downloaded from files.cie.co.at on 2026-11-16, sha256 fa663e3535a7e0763a745993a1f0a192eb0275ac46ad2d1befd7626841e713c1 (the metadata's checksum). It is compiled into the plugin (CMake turns the CSV into a source file) and summed, with D65, into the plugin's colour table, an adaptation of the data: the table and the colour table derived from it are CC BY-SA 4.0, not MIT; the code is MIT.

### CIE standard illuminant D65 — CIE

<https://doi.org/10.25039/CIE.DS.hjfjmt59>  
Licence: Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)  
Copyright: International Commission on Illumination (CIE)

data/CIE_std_illum_D65.csv (300–830 nm, 1 nm) and data/CIE_std_illum_D65.csv_metadata.json, unchanged. Source: ISO/CIE 11664-2:2022 Table B.1; downloaded from files.cie.co.at on 2026-11-16, sha256 e76f210bffff3d552ef7113025da5f325d5dfec200dd4b878b1a2f3a507032cb (the metadata's checksum). Compiled in and summed into the colour table with the colour-matching functions, under the same terms: CC BY-SA 4.0, not MIT.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### Multilayer optical calculations — S. J. Byrnes

<https://arxiv.org/abs/1603.02720>

arXiv:1603.02720: the Fresnel coefficients and the 2 × 2 transfer matrix the harness checks the plugin's Airy reflectance against.

### IAPWS R9-97, the refractive index of ordinary water substance; NIST Chemistry WebBook — IAPWS; NIST (SRD 69)

<https://www.iapws.org/relguide/rindex.pdf>

The film's refractive index at 20 °C from 360 to 830 nm, checked against the release's own Table 3; the density of water at 20 °C (998.20715 kg/m³) from the NIST WebBook.

### Self-similar and Universal Dynamics in Drainage of Mobile Soap Films — L. Monier et al.

<https://arxiv.org/abs/2401.03931>

arXiv:2401.03931 (2024): the drainage law h = A(τ) · G(y) and its master curve, fitted to ten points digitised from their Fig. 3b; the surface tension of their solutions (35 mN/m) for the burst.

### Bubble and Foam Chemistry — R. J. Pugh

Cambridge University Press (2016), ch. 3: black films, Newton black film at about 4 nm, the last interference maximum and minimum at 546 nm (102 and 204 nm), which the harness checks the colour table against.

### The history of black-film thicknesses — V. Ziapkoff et al.

<https://arxiv.org/abs/2511.21383>

arXiv:2511.21383: Reinold and Rücker's common black film of about 12 nm, as they report it.

### Bursting of soap films — A. Savva and J. W. M. Bush

J. Fluid Mech. 626 (2009): the Taylor–Culick speed u = √(2γ/(ρh)) of a hole's rim, the burst's speed through film of every thickness.

## Standards and published specifications

What the implementation is measured against.

- **IEC 61966-2-1 (sRGB)** — The clip's decoding and the output's encoding, and the matrix from CIE XYZ to linear sRGB the colour table is built with (source/Film.cpp, source/Shaders.cpp; the harness's own copy in tools/bbtest).
- **CIE 018:2019 and ISO/CIE 11664-1/-2** — The 1931 standard observer and illuminant D65, as tabulated in the datasets above.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
