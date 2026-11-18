/**
 * Bubble — browser demo.
 *
 * The clip reflected in a draining soap film (SW Bubble, BB01, an effect).
 * The one idea, from AGENTS.md: a soap film is a sheet of water a few hundred
 * nanometres thick, light reflected from its two faces interferes, so its
 * colour is its thickness; gravity drains it from the top by Monier et al.'s
 * self-similar law; under 30 nm it jumps to black film; and then it bursts, the
 * hole opening at the Taylor–Culick speed. The picture is the clip, as the
 * scene the film reflects, tinted by the film. Nothing is drawn.
 *
 * ---------------------------------------------------------------------------
 * What runs here, and what does not
 * ---------------------------------------------------------------------------
 *
 * **This page does not port the plugin. It runs it.** `bubble-core.wasm` is
 * the plugin's own C++ compiled UNMODIFIED with emscripten
 * (demo/tools/build-wasm.sh): `source/Bubble.cpp` — the plugin class, with its
 * constructor, the host clock and its vote on seconds or milliseconds, the
 * film's clock (draining, bursting, empty, refilled), the R32F displacement
 * grid and its 1/720 s advection steps, every uniform it sets and the GL state
 * it saves and puts back — and everything it calls (Film, Controls,
 * PassBuffer, Shaders, Diag), with the FFGL SDK's CFFGLPlugin, its parameter
 * bookkeeping, FFGLShader and FFGLFBO. The colour table (the Airy reflectance
 * of water summed over the CIE 1931 functions under D65, 2001 × 121 films) and
 * the drainage law's G table are that C++, in double, on this page's thread,
 * from the CIE's own tables (CieTables.inc, made from data/*.csv by the
 * repository's cmake/CieTables.cmake, CC BY-SA 4.0). Its GL calls go to this
 * page's WebGL2 context through emscripten's GL library.
 *
 * What is the page's and not the plugin's:
 *
 *   - **The host.** demo/wasm/glue.cpp constructs the plugin (what
 *     EffectPlugin.cpp's registration does in a bundle), reads its parameter
 *     declarations back through the SDK's host getters — the panel below is
 *     built from them, not from a list typed here — and forwards
 *     SetFloatParameter, SetTime (the page's clock, in seconds) and
 *     ProcessOpenGL with one input texture, as tools/bbtest's Rig does.
 *   - **Five GL entry points** (demo/wasm/gl_shim.cpp). glShaderSource: the
 *     plugin hands GL desktop GLSL 4.10; `shaderSource()` below refuses any
 *     text that is not byte for byte one of this page's assemblies of its copy
 *     (shaders.js, held to source/Shaders.cpp by demo/tools/check_shaders.py),
 *     then applies the kit's `port()` — the version line and the ES precision
 *     defaults, nothing else. glEnable / glDisable / glIsEnabled:
 *     GL_PROGRAM_POINT_SIZE, which the plugin saves and restores every frame,
 *     does not exist in WebGL2. glTexImage2D: the SDK allocates the grid's
 *     R32F buffers with a client format ES does not pair with R32F.
 *   - **The panel's read-outs.** The number beside a slider is Controls.cpp's
 *     conversion, compiled in (glue.cpp's bb_convert); the units printed after
 *     it, the tooltips and the presets are this page's. The presets are
 *     written in micrometres, times, mm/s and degrees and go to the host's
 *     0..1 through the plugin's own inverse functions (bb_param_from).
 *   - **Pop and New Film are buttons.** They are FF_TYPE_EVENT; a press sends
 *     1 then 0, as a host's button does, and the plugin latches the rising
 *     edge for its next frame.
 *   - **The line under the picture**: the plugin's own state, through the
 *     accessors its harness reads, and Film.h's functions of it. It reports;
 *     it measures nothing. Its flash warning is the page's arithmetic.
 *   - **The start gate.** Nothing is mounted, so nothing draws, until the
 *     visitor has pressed Start under the photosensitivity warning at the top
 *     of the page (index.html).
 *
 * **The About block is not on the panel**, as on every page in the suite.
 */

import { mountDemo } from './vendor/demo.js';
import { port, GLError } from './vendor/gl.js';
import * as S from './shaders.js';
import createBubble from './bubble-core.js';

//===========================================================================
// FFGL.h's parameter types, by number: what GetParamType returns.
//===========================================================================

const FF_TYPE_BOOLEAN = 0;
const FF_TYPE_EVENT = 1;
const FF_TYPE_STANDARD = 10;
const FF_TYPE_OPTION = 11;
const FF_TYPE_TEXT = 100;

//===========================================================================
// The GLSL: the page's checked copy, assembled as the plugin assembles it
// (ASSEMBLY is read out of AssembleVertex() and Assemble() by
// check_shaders.py), one text per stage of each of the two passes.
//===========================================================================

const PAGE_SHADERS = Object.entries(S.ASSEMBLY).map(([name, pieces]) => ({
  name,
  stage: name.endsWith('_vertex') ? 'vertex' : 'fragment',
  text: pieces.map((p) => S[p]).join(''),
}));

/** Which stages the compiled plugin handed over, and whether each was ours. */
const shaderCheck = { matched: new Set(), refused: 0 };

/**
 * Called by gl_shim.cpp's glShaderSource with the text the compiled plugin
 * handed to GL. Anything but one of this page's own assemblies is refused (an
 * empty source, so the plugin's InitGL fails and says so) rather than compiled.
 * Both vertex stages are the same text, so a vertex match names the first.
 */
function shaderSource(stage, text) {
  const ours = PAGE_SHADERS.find((s) => s.stage === stage && s.text === text);
  if (!ours) {
    shaderCheck.refused += 1;
    return '';
  }
  shaderCheck.matched.add(ours.name);
  return port(ours.text, { stage });
}

//===========================================================================
// The start gate's elements (index.html). Found before anything else so a
// failure below can still say so where the visitor is looking.
//===========================================================================

const startButton = document.getElementById('bb-start');
const startNote = document.getElementById('bb-start-note');

//===========================================================================
// The module.
//===========================================================================

const printed = [];
let core;
try {
  core = await createBubble({
    bubbleShaderSource: shaderSource,
    print: (text) => { printed.push(text); console.log(text); },
    printErr: (text) => { printed.push(text); console.warn(text); },
  });
} catch (error) {
  const message = `The demo could not start: this browser would not run its WebAssembly (${error.message}). The plugin, its source and its downloads are at https://github.com/stoatworks-labs/bubble.`;
  if (startNote) startNote.textContent = message;
  else document.querySelector('#demo').textContent = message;
  throw error;
}

const text = (pointer) => core.UTF8ToString(pointer);

//===========================================================================
// The declarations, read from the plugin's constructor through the SDK.
//===========================================================================

function declarations() {
  const h = core._bb_new();
  const list = [];
  const n = core._bb_param_count(h);
  for (let i = 0; i < n; i += 1) {
    const type = core._bb_param_type(h, i);
    const d = { index: i, name: text(core._bb_param_name(h, i)), type, group: text(core._bb_param_group(h, i)) };
    d.default = type === FF_TYPE_TEXT ? null : core._bb_param_default(h, i);
    if (type === FF_TYPE_OPTION) {
      d.elements = [];
      d.values = [];
      for (let e = 0; e < core._bb_param_element_count(h, i); e += 1) {
        d.elements.push(text(core._bb_param_element_name(h, i, e)));
        d.values.push(core._bb_param_element_value(h, i, e));
      }
    }
    list.push(d);
  }
  const inputs = [core._bb_min_inputs(h), core._bb_max_inputs(h)];
  const time = core._bb_time_supported(h) === 1;
  core._bb_delete(h);
  return { list, inputs, time };
}

const DECLARED = declarations();
if (DECLARED.inputs[0] !== 1 || DECLARED.inputs[1] !== 1) {
  throw new Error(`SW Bubble declares ${DECLARED.inputs.join('–')} inputs; this page hands it exactly one`);
}
if (!DECLARED.time) {
  throw new Error('SW Bubble no longer declares SetTimeSupported; this page hands it the time every frame');
}

//===========================================================================
// The read-outs: Controls.cpp's number, this page's unit.
//===========================================================================

const sig = (n) => (Math.abs(n) >= 100 ? n.toFixed(0) : Math.abs(n) >= 10 ? n.toFixed(1) : Math.abs(n) >= 1 ? n.toFixed(2) : n.toFixed(3));
const UNITS = {
  Thickness: (um) => `${um.toFixed(2)} µm`,
  Drainage: (x) => `${sig(x)} ×`,
  Swirl: (mm) => `${mm.toFixed(1)} mm/s`,
  Burst: (reach) => (reach >= 1 ? 'never' : `${reach.toFixed(2)} down`),
  'Slow Motion': (x) => `${sig(x)} ×`,
  Refill: (s) => `${s.toFixed(2)} s`,
  Angle: (deg) => `${deg.toFixed(1)}°`,
  Exposure: (level) => `× ${sig(level)}`,
};

function readout(d) {
  return (v) => {
    const x = core._bb_convert(d.index, Math.fround(v));
    if (Number.isNaN(x)) return v.toFixed(2);
    return (UNITS[d.name] ?? ((y) => y.toFixed(3)))(x);
  };
}

/** Tooltips: this page's words, from the README's description of each control. */
const HINTS = {
  Thickness: 'The film’s thickness at mid-height when it is pulled, 0.2–4 µm. It then thins from the top by the drainage law; a thinner film shows its colours, and its black, sooner.',
  Drainage: 'How fast it drains, 0.1–10 × the real 25 mm film’s rate (logarithmic): the law’s clock, so the same bands arrive sooner or later, not different ones.',
  Swirl: 'The stirring, 0–20 mm/s: the bound on the speed of six smooth modes that carry the film, and its bands, about. A chosen stirring, not a simulation of the film’s own flow. 0 is the drainage law alone.',
  'Black Film': 'What the film becomes below 30 nm: Common black film, 12 nm (Reinold & Rücker), or Newton black film, 4 nm (Pugh). Both are black to the eye; Newton’s is the darker.',
  Burst: 'How far down the black film comes before the film bursts, 0–1 of the frame; 1 is never. At 0 a new film bursts as soon as it is pulled.',
  'Slow Motion': 'How slowly the burst is shown, 1–1000 × (logarithmic). In real time the whole burst is over inside one frame at 60 frames a second.',
  Refill: 'How long the frame stays empty (black) after a burst before a new film is pulled, 0–10 s.',
  Pop: 'Burst the film now. FF_TYPE_EVENT: a press sends 1 then 0, and the plugin bursts on the rising edge.',
  'New Film': 'Pull a fresh film now, whatever this one is doing. FF_TYPE_EVENT: a press sends 1 then 0.',
  Angle: 'The angle of incidence, 0–60°. Tilting the film shortens the light’s path through it, so its colours move as if it were thinner. The film is flat and seen at one angle.',
  Exposure: 'Linear light, 0–200. A soap film reflects about 4% of the scene, so the default is large: 86.2515, set on Resolume’s 33 demo clips so the brightest 1% of the median clip just reaches full scale.',
  Mix: 'The film against the clip, premultiplied. At 0 the clip comes through untouched; at 1 the output is opaque, and a burst film shows black.',
};

//===========================================================================
// Kit parameters from the declarations. The id is the name as Arena
// addresses it (lower case, spaces removed).
//===========================================================================

const KIT_TYPES = {
  [FF_TYPE_STANDARD]: 'standard',
  [FF_TYPE_OPTION]: 'option',
  [FF_TYPE_BOOLEAN]: 'boolean',
  [FF_TYPE_EVENT]: 'boolean',
};

/** The kit reads these from the query string itself; a parameter must not be one. */
const RESERVED_QUERY = new Set(['embed', 'size', 'clip', 'bg']);
const addressOf = (name) => {
  const address = name.toLowerCase().replace(/\s+/g, '');
  return RESERVED_QUERY.has(address) ? `param-${address}` : address;
};

const onPanel = (d) => d.group !== 'About';

function kitDefault(d) {
  switch (d.type) {
    case FF_TYPE_OPTION: {
      const i = d.values.findIndex((v) => v === d.default);
      return i >= 0 ? i : Math.round(d.default);
    }
    case FF_TYPE_BOOLEAN:
    case FF_TYPE_EVENT: return d.default > 0.5 ? 1 : 0;
    default: return d.default;
  }
}

const PARAMS = DECLARED.list.filter(onPanel).map((d) => {
  const type = KIT_TYPES[d.type];
  if (type === undefined) throw new Error(`no kit control for FFGL type ${d.type} (${d.name})`);
  const p = { id: addressOf(d.name), name: d.name, type, group: d.group, hint: HINTS[d.name], ff: d, default: kitDefault(d) };
  if (d.type === FF_TYPE_OPTION) p.elements = d.elements;
  if (d.type === FF_TYPE_STANDARD) p.display = readout(d);
  return p;
});

const named = (name) => {
  const p = PARAMS.find((x) => x.name === name);
  if (!p) throw new Error(`SW Bubble declares no control called ${name}`);
  return p;
};

/** The value the host would send for a kit control, or null for none. */
function hostValue(p, v) {
  const d = p.ff;
  switch (d.type) {
    case FF_TYPE_OPTION: return Math.fround(d.values[Math.round(v)] ?? 0);
    case FF_TYPE_BOOLEAN: return v > 0.5 ? 1 : 0;
    case FF_TYPE_EVENT: return null; // sent by its button, as a press
    default: return Math.fround(v);
  }
}

//===========================================================================
// The presets: whole states on top of the constructor's defaults, by the
// plugin's own names. A number in { units } is in the plugin's units
// (micrometres, times, mm/s, degrees) and is converted by the plugin's own
// inverse (bb_param_from); an option is its element's name. They are the
// page's choices, and none of them bursts a film more than once in a few
// seconds.
//===========================================================================

const u = (units) => ({ units });
const PRESET_UNITS = [
  ['The defaults: a 1.5 µm film draining at 4 ×, bursting about 30 s in', {}],
  ['Colour sooner: a 0.6 µm film draining at 10 ×', { Thickness: u(0.6), Drainage: u(10) }],
  ['The drainage law alone: no stirring (Swirl 0)', { Swirl: u(0) }],
  ['Stirred hard: 15 mm/s', { Swirl: u(15) }],
  ['Newton black film, tilted to 45°', { 'Black Film': 'Newton', Angle: u(45), Thickness: u(0.6), Drainage: u(10) }],
  ['A slow burst: 1000 × slow motion, bursting early', { Thickness: u(0.6), Drainage: u(10), Burst: u(0.15), 'Slow Motion': u(1000), Refill: u(3) }],
  ['A film that never bursts (Burst 1)', { Burst: u(1) }],
];

function kitPresets() {
  const out = {};
  for (const [title, spec] of PRESET_UNITS) {
    const values = {};
    for (const [name, value] of Object.entries(spec)) {
      const p = named(name);
      if (p.type === 'option') {
        const slot = p.elements.indexOf(value);
        if (slot < 0) throw new Error(`preset “${title}”: ${name} has no element ${value}`);
        values[p.id] = slot;
      } else {
        const host = typeof value === 'object' ? core._bb_param_from(p.ff.index, value.units) : value;
        if (!Number.isFinite(host)) throw new Error(`preset “${title}”: ${name} has no conversion`);
        values[p.id] = host;
      }
    }
    out[title] = values;
  }
  return out;
}

//===========================================================================
// The renderer: one plugin instance, driven as a host drives it.
//===========================================================================

let renderer = null;

function createRenderer(gl) {
  // emscripten's GL library, pointed at the kit's context. Nothing is turned
  // on behind the kit's back: no extensions are enabled on its behalf (the
  // kit has enabled EXT_color_buffer_float, needFloat, or refused to start:
  // the displacement grid is an R32F render target).
  const handle = core.GL.registerContext(gl, { majorVersion: 2, minorVersion: 0, enableExtensionsByDefault: false });
  core.GL.makeContextCurrent(handle);
  core._bb_prepare_log();

  let instance = 0;
  let pushed = new Map();
  let clipName = 0;
  let clipTexture = null;
  let frames = 0;
  let raster = [0, 0];
  let settleForTest = false;///< crosscheck.py only: the plugin's SetClockScaleForTest, as bbtest's Rig

  /** A kit texture under a GL name the plugin can bind, in emscripten's tables. */
  function glName(texture) {
    if (texture === clipTexture) return clipName;
    if (clipName) core.GL.textures[clipName] = null;
    clipName = core.GL.getNewId(core.GL.textures);
    core.GL.textures[clipName] = texture;
    texture.name = clipName;
    clipTexture = texture;
    return clipName;
  }

  function start(width, height) {
    instance = core._bb_new();
    pushed = new Map();
    if (settleForTest) core._bb_settle_clock(instance, 1.0);
    if (!core._bb_init_gl(instance, width, height)) {
      const why = shaderCheck.refused > 0
        ? 'the GLSL the compiled plugin handed to GL is not this page’s checked copy of source/Shaders.cpp, so it was not compiled'
        : `the plugin’s InitGL failed${printed.length ? `: ${printed.slice(-3).join(' ')}` : ''}`;
      throw new GLError(`SW Bubble could not start: ${why}.`);
    }
  }

  function push(params) {
    for (const p of PARAMS) {
      const value = hostValue(p, params.get(p.id));
      if (value === null || pushed.get(p.id) === value) continue;
      pushed.set(p.id, value);
      core._bb_set_float(instance, p.ff.index, value);
    }
  }

  const setting = (name) => core._bb_convert(named(name).ff.index, core._bb_get_float(instance, named(name).ff.index));

  return {
    render({ input, params, width, height, time }) {
      core.GL.makeContextCurrent(handle);
      if (!instance) start(width, height);
      push(params);
      core._bb_set_time(instance, time);
      if (!core._bb_process(instance, glName(input.texture), input.width, input.height)) {
        const log = this.log().trim().split('\n').filter(Boolean).slice(-1)[0] ?? '';
        throw new GLError(`The plugin’s ProcessOpenGL failed.${log ? ` Its log: ${log.replace(/^\S+ /, '')}` : ''}`);
      }
      // For anything checking the page from outside: frames drawn by this
      // page (across instances), the film's generation and its phase.
      frames += 1;
      raster = [input.width, input.height];
      gl.canvas.dataset.frames = String(frames);
      gl.canvas.dataset.generation = String(core._bb_clock(instance, 1));
      gl.canvas.dataset.phase = ['draining', 'bursting', 'empty'][core._bb_clock(instance, 0)];
    },
    /** An FF_TYPE_EVENT press: 1 then 0, the plugin latching the rising edge. */
    press(index) {
      if (!instance) return false;
      core._bb_set_float(instance, index, 1);
      core._bb_set_float(instance, index, 0);
      return true;
    },
    stats() {
      if (!instance || !frames) return null;
      const [width, height] = raster;
      const clock = (i) => core._bb_clock(instance, i);
      const drainage = setting('Drainage');
      return {
        phase: clock(0),
        generation: clock(1),
        age: clock(2),
        tau: clock(3),
        burstSeconds: clock(5),
        emptySeconds: clock(6),
        front: core._bb_front(instance),
        midNm: core._bb_mid_nm(instance),
        burstTau: core._bb_burst_tau(instance),
        burstSpan: core._bb_burst_span(instance, width / height),
        drainage,
        refill: setting('Refill'),
        reach: setting('Burst'),
        steps: core._bb_steps_run(instance),
        grid: [core._bb_grid_width(instance), core._bb_grid_height(instance)],
        tintSeconds: core._bb_tint_seconds(),
        width,
        height,
      };
    },
    log() {
      try {
        return core.FS.readFile(text(core._bb_log_path()), { encoding: 'utf8' });
      } catch {
        return '';
      }
    },
    /**
     * demo/tools/crosscheck.py only: a fresh instance on the next frame, with
     * the plugin's clock unit settled to seconds through its own test seam
     * (SetClockScaleForTest), as bbtest's Rig does before InitGL. The page
     * never calls it.
     */
    restartForTest() {
      core.GL.makeContextCurrent(handle);
      if (instance) core._bb_delete(instance);
      instance = 0;
      settleForTest = true;
    },
    shaders: shaderCheck,
  };
}

//===========================================================================
// What this page does not reproduce. The kit adds its four.
//===========================================================================

const DIFFERENCES = [
  'This page runs the plugin rather than a port of it. bubble-core.wasm is source/Bubble.cpp — the plugin class: its constructor, the host clock and its vote on seconds or milliseconds, the film’s clock (draining, bursting, empty, refilled), the R32F displacement grid and its 1/720 s advection steps, every uniform and the GL state it saves and puts back — and everything it calls (Film, Controls, PassBuffer, Shaders, Diag), with the FFGL SDK’s CFFGLPlugin, parameter bookkeeping, FFGLShader and FFGLFBO, all compiled unmodified by emscripten (demo/tools/build-wasm.sh). The colour table (2001 optical thicknesses in 2 nm steps by 121 angles in 0.5° steps: the Airy reflectance of water summed over the CIE 1931 2° functions under D65) and the drainage law’s G table are that C++, in double, from the CIE’s own tables, generated from the repository’s data/*.csv by its own cmake/CieTables.cmake and compiled in unedited (CC BY-SA 4.0). Only the registration file is left out; the page constructs the plugin itself, as the plugin’s harness does. The panel is read back from the plugin’s own declarations through the SDK’s host getters, and the numbers beside the sliders are Controls.cpp’s conversions, compiled in; their units, every tooltip and the presets are this page’s.',
  'The GLSL is the plugin’s: demo/shaders.js is spliced from source/Shaders.cpp, with the order AssembleVertex() and Assemble() join the pieces in, and the repository’s verify script fails if a character differs, or if any uniform a pass declares is not set by the setter its type takes. At start-up the page also requires each of the four stages the compiled plugin hands to glShaderSource to equal its copy, and refuses to compile anything else. The kit’s port() then changes the version line and adds the ES precision defaults (highp). GL is emscripten’s WebGL2 library on this page’s context, not a GL 4.1 driver, and five entry points are the page’s (demo/wasm/gl_shim.cpp): glShaderSource, for that check and port; glEnable, glDisable and glIsEnabled, because GL_PROGRAM_POINT_SIZE — state the plugin saves and restores each frame, as a plugin in Resolume must — does not exist in WebGL2 (it reads as off here; the plugin draws no points); and glTexImage2D, because the FFGL SDK allocates the grid’s R32F buffers with a client format (RGBA) that desktop GL ignores when there is no data and WebGL2 refuses, so the shim gives that one allocation the format ES pairs with R32F (RED). The plugin’s two table uploads go through unchanged.',
  'The displacement grid is two R32F render targets, as in the plugin, and rendering into a 32-bit float buffer needs WebGL2’s EXT_color_buffer_float: a browser without it is told so and the demo does not start, rather than being handed an 8-bit grid. The grid, the colour table (RGBA32F) and the G table (R32F) are read texel by texel (texelFetch, nearest) with the plugin’s own bilinear by hand, so no float filtering is needed. The formats are the plugin’s, but the arithmetic is this browser’s GPU (through ANGLE, on a Mac through Metal), not the Mac’s GL driver: the stirring runs one advection pass per 1/720 s of film age (12 a frame at 60 frames a second), and where the two round a step differently the stirred film can come out an 8-bit code apart in a pixel here and there (in the comparison below, 1 or 2 of 172,800 values after 360 to 1,440 steps).',
  'There are no threads. The plugin sums its colour table’s 121 rows on helper threads; this WebAssembly is built without them, so its libc reports one processor and the plugin’s own loop sums every row on the page’s thread, once, on the first frame after Start (the line under the picture says how long it took; the page waits for it). Each row is summed whole either way, so the table is the same. WebAssembly is not the Mac’s arm64: it has no fused multiply-add, and its pow, exp, log, sin and cos are musl’s, not Apple’s, so the tables’ doubles, and A(τ), can differ from the plugin built for a Mac in the last digits before they are rounded to float.',
  'The host’s clock is the page’s: the time handed to SetTime every frame is the kit’s, in seconds, added up from the display’s frame intervals (each capped at a tenth of a second) and standing still while paused. The plugin votes on whether its host sends seconds or milliseconds against the wall clock for its first frames, as it does in Resolume, and runs on the wall clock until the vote is in (a few frames); after that the film’s age is the page’s time, so Pause stands the film still and Step moves it on by a sixtieth of a second, Slow Motion and all. Restart puts the clip back to its start, which the plugin takes as the host’s clock jumping back: no time passes and the film carries on. New Film starts a new film. A frame here is one of the display’s refreshes, not Resolume’s output frame; the film’s clock is in whole microseconds of host time, so it is the same film at any frame rate.',
  'The raster is the page’s Composition menu (960 × 540 unless you pick another), or your own image’s size. The film is 25 mm tall at every raster and the grid is 512 cells wide with the frame’s aspect, so the same settings make the same film at every size; changing the raster’s aspect resamples the grid, as the plugin does in a host.',
  'The input is the kit’s texture, unpadded: Width and HardwareWidth are the same, and the page’s viewport is the whole canvas at the origin. The plugin reads its input texel by texel at the viewport’s offset, so neither should matter, but a padded Resolume texture and a viewport off the origin are not exercised here. The output is the page’s 8-bit canvas: the plugin’s float picture is rounded to 8 bits once, at the end.',
  'Alpha: the kit hands the plugin premultiplied colour (its clips and your own image alike are premultiplied on upload), and the plugin treats the clip’s colour as straight and weights the reflected scene by α, so over a half-transparent edge the light is weighted twice. The page’s clips are all opaque, which is why the kit’s shape-on-transparency clip is not among them; an image of your own with transparency would show the double weighting at its edges. Which of the two Resolume hands an effect is an open question in the plugin’s own AGENTS.md.',
  'Compared with the plugin’s harness once, on an Apple M4 Max when this page was built: the same input frame (the colour bars, read back from this page at Mix 0) through bbtest --pipe — the real plugin class in a GL 4.1 context — at 320 × 180, with the same host values and the same host clock (sixtieths of a second from 0, the clock unit settled to seconds through the plugin’s own test seam on both sides), the page stepped one frame at a time, in eight cases read at 13 frames: the defaults at the first frame and stirred for 1 s; a 0.6 µm film at 10 × unstirred for 2 s, with common black film and with Newton black film at 45°; stirred at 15 mm/s for 2 s; Pop, and 10 frames of the burst; New Film; Mix 0.5 with Exposure 30. Nine came out identical byte for byte. The other four, all stirred (30 to 120 frames, 360 to 1,440 advection steps), differed in 1 or 2 of 172,800 colour values, by one 8-bit code. The harness one frame later, or with Thickness one slider step off, differs from the page in 40,549 and 30,097 values. That was this page through ANGLE on Metal against the harness through Apple’s OpenGL, on the same GPU; another GPU rounds its own way. demo/tools/crosscheck.py in the repository repeats it; nothing repeats it by itself.',
  'Pop and New Film are FF_TYPE_EVENT in the plugin and buttons here: a press sends 1 then 0, and the plugin acts on the rising edge at its next frame (while paused, a press draws that one frame, and the burst then waits for the clock). The About block (a credit line and its buttons) is declared by the plugin and left off this panel.',
  'The warning at the top, the start button, the line under the picture, its flash warning and the Presets menu are the page’s, not the plugin’s. The line’s burst time and its flash rate are the page’s arithmetic from Film.h’s own functions (the burst’s drainage time, the burst’s span at this Slow Motion, Refill); they say nothing about a clip of your own that flickers.',
  'Nothing here is measured, and the plugin has never been loaded into Resolume. Its harness, bbtest, drives the same class headlessly and checks the physics: the colour of 72 uniform films against its own transfer-matrix × CIE × D65 sum, the black front on the drainage law’s row in every column, the film bit-identical at 24, 60 and 144 frames a second, the hole the Taylor–Culick disc to the pixel, and 22 wrong models and 65 one-character mutants caught. That harness, not this page, is the reason to believe the film.',
];

//===========================================================================
// Start: nothing is mounted until the visitor has read the warning.
//===========================================================================

let mounted = null;

/** WebGL2 and EXT_color_buffer_float, asked of a throwaway canvas first. */
function floatTargets() {
  try {
    const probe = document.createElement('canvas').getContext('webgl2');
    if (!probe) return 'This browser has no WebGL2, so the demo cannot run here: the plugin is OpenGL 4.1, and WebGL2 is the nearest a browser has.';
    const ok = probe.getExtension('EXT_color_buffer_float') !== null;
    probe.getExtension('WEBGL_lose_context')?.loseContext();
    return ok ? null : 'This browser’s WebGL2 has no EXT_color_buffer_float, so it cannot render into the plugin’s R32F displacement grid, and the demo cannot run here. (Desktop OpenGL, where the plugin runs, needs no extension for it.)';
  } catch (error) {
    return `This browser would not make a WebGL2 context (${error.message}), so the demo cannot run here.`;
  }
}

if (startButton) {
  const missing = floatTargets();
  if (missing) {
    if (startNote) startNote.textContent = missing;
  } else {
    startButton.hidden = false;
    startButton.addEventListener('click', () => {
      startButton.disabled = true;
      if (startNote) startNote.textContent = 'Starting: the plugin sums its colour table first, once (a moment).';
      // Let the note paint before the first frame holds the thread.
      requestAnimationFrame(() => setTimeout(() => {
        start();
        startButton.remove();
        document.getElementById('photosensitivity')?.classList.add('is-started');
        if (startNote) startNote.textContent = 'Started. Pause, under the picture, stands the film still.';
      }, 0));
    }, { once: true });
  }
}

function start() {
  mounted = mountDemo({
    name: 'Bubble',
    pluginId: 'BB01',
    kind: 'effect',
    tagline:
      'The clip reflected in a draining soap film, for Resolume. The film is a sheet of water a few hundred nanometres thick, and its colour is its thickness: light from its two faces interferes, so bands of Newton’s colours slide down as gravity drains it from the top, the top goes silvery and then black with a sharp edge, the colours swirl when it’s stirred, and then it bursts, the hole racing out faster through thin film than thick. None of it is drawn.',
    repo: 'https://github.com/stoatworks-labs/bubble',

    blurb:
      'It is the plugin’s own C++ — the plugin class, its film’s clock and its colour table summed from the CIE’s data, in double — compiled unmodified to WebAssembly, drawing with its own GLSL in WebGL2, on generated clips in this page. The host is this page.',

    // The displacement grid's two buffers are R32F render targets, as in the plugin.
    needFloat: true,
    // The plugin writes alpha mix( a, 1, Mix ): 1 over the page's opaque clips.
    showBackdrop: false,

    // All opaque: the kit's 'alpha' clip is left out because the kit's
    // premultiplied colour is not the straight colour the plugin weights by
    // alpha (see the notes at the foot).
    sources: ['scene', 'bars', 'ramp', 'spot', 'grid', 'detail'],

    params: PARAMS,
    presets: kitPresets(),
    differences: DIFFERENCES,

    createRenderer: (gl) => {
      renderer = createRenderer(gl);
      return renderer;
    },
  });
  if (!mounted) return;
  const embed = new URLSearchParams(window.location.search).has('embed')
    && new URLSearchParams(window.location.search).get('embed') !== '0';
  if (!embed) {
    buildEventButtons();
    buildStatusLine();
  }
}

//---------------------------------------------------------------------------
// Pop and New Film: FF_TYPE_EVENT, a press and a release.
//---------------------------------------------------------------------------
function buildEventButtons() {
  const rowOf = (p) => [...document.querySelectorAll('.prow')]
    .find((row) => row.querySelector('.prow__name')?.textContent === p.name);
  for (const p of PARAMS) {
    if (p.ff.type !== FF_TYPE_EVENT) continue;
    const toggle = rowOf(p)?.querySelector('.prow__toggle');
    if (!toggle) continue;
    const press = document.createElement('button');
    press.type = 'button';
    press.className = 'prow__toggle bb-press';
    press.id = `p-${p.id}`;
    press.textContent = p.name;
    if (p.hint) press.title = p.hint;
    press.addEventListener('click', () => {
      if (renderer?.press(p.ff.index)) mounted.redraw();
    });
    toggle.replaceWith(press);
  }
}

//---------------------------------------------------------------------------
// The line under the picture: the plugin's own state, through the accessors
// its harness reads, and Film.h's functions of it. It reports; it measures
// nothing. The flash warning is the page's arithmetic.
//---------------------------------------------------------------------------
function buildStatusLine() {
  const stage = document.querySelector('.stage');
  if (!stage) return;
  const line = document.createElement('p');
  line.className = 'stage__status';
  line.id = 'bubble-status';
  line.setAttribute('aria-live', 'off');
  const caution = document.createElement('p');
  caution.className = 'stage__status bb-caution';
  caution.id = 'bubble-caution';
  caution.setAttribute('role', 'status');
  caution.hidden = true;
  const log = document.createElement('p');
  log.className = 'stage__status bb-log';
  log.id = 'bubble-log';
  log.setAttribute('aria-live', 'off');
  stage.append(line, caution, log);

  const secs = (s) => (s >= 100 ? `${s.toFixed(0)} s` : s >= 10 ? `${s.toFixed(1)} s` : `${s.toFixed(2)} s`);
  const nm = (h) => (h >= 1000 ? `${(h / 1000).toFixed(2)} µm` : `${h.toFixed(0)} nm`);

  let lastLine = '';
  let lastCaution = '';
  let lastLog = '';
  setInterval(() => {
    const s = renderer?.stats();
    if (!s) return;
    let what;
    if (s.phase === 0) {
      const left = Number.isFinite(s.burstTau) ? Math.max(0, (s.burstTau - s.tau) / s.drainage) : Infinity;
      const front = Math.max(0, 1 - s.front);
      what = `Film ${s.generation}, draining: ${secs(s.age)} old, ${nm(s.midNm)} at mid-height where it is not stirred,`
        + ` the black film ${front < 0.0005 ? 'not yet formed' : `${front.toFixed(3)} of the way down`}`
        + ` · ${Number.isFinite(left) ? `bursts in ${secs(left)} (when the black is ${s.reach.toFixed(2)} down)` : 'never bursts (Burst 1)'}`;
    } else if (s.phase === 1) {
      what = `Film ${s.generation}, bursting: the hole is ${secs(s.burstSeconds)} old of ${secs(s.burstSpan)} at this Slow Motion`;
    } else {
      what = `Empty after film ${s.generation}'s burst: ${secs(s.emptySeconds)} of ${secs(s.refill)} before a new film`;
    }
    const next = `${what} · ${s.steps.toLocaleString('en-GB')} advection steps run · grid ${s.grid[0]} × ${s.grid[1]} at ${s.width} × ${s.height}`
      + `${s.tintSeconds > 0 ? ` · colour table summed in ${s.tintSeconds.toFixed(2)} s` : ''}`;
    if (next !== lastLine) {
      lastLine = next;
      line.textContent = next;
    }

    // How often a fresh film is pulled and burst at these settings: its age at
    // the burst (BurstTime from birth, tau 0, at this Drainage), the burst's
    // span (BurstDuration x Slow Motion) and Refill. Each is one flash, film
    // to black and back. The page's arithmetic, from the plugin's functions.
    let warn = '';
    if (Number.isFinite(s.burstTau)) {
      const cycle = Math.max(0, s.burstTau) / s.drainage + s.burstSpan + s.refill;
      const rate = cycle > 0 ? 1 / cycle : Infinity;
      if (rate > 3) {
        // A display shows one change a refresh, so past 30 a second the
        // picture flips between film and black as fast as it can.
        const often = rate > 30 ? 'as fast as the display can show it (every frame or two)' : `${rate.toFixed(1)} times a second`;
        warn = `Flashing: at these settings a new film is pulled and burst ${often}, faster than three times a second.`
          + ' Raise Burst, Slow Motion or Refill, or press Pause.';
      }
    }
    if (warn !== lastCaution) {
      lastCaution = warn;
      caution.textContent = warn;
      caution.hidden = !warn;
    }

    const textLog = renderer.log();
    if (textLog === lastLog) return;
    lastLog = textLog;
    const lines = textLog.trim().split('\n').filter(Boolean).map((l) => l.replace(/^\S+ (INFO |WARN |ERROR) bubble: /, '$1 '));
    log.textContent = lines.length ? `The plugin’s own log (Diag.cpp, in this page’s memory):\n${lines.slice(-2).join('\n')}` : '';
  }, 250);
}

// For checking the page from outside (the crosscheck against bbtest reads the
// declarations and the inverse conversions through these).
export { renderer, mounted, PARAMS, DECLARED, core };
