"""The browser demo against the plugin's harness: the same input frame, the same
raster, the same settings, the same host clock, two renderers.

    python3 demo/tools/crosscheck.py --bbtest build/bbtest [--keep DIR]

Needs a GPU, Google Chrome, numpy, a built bbtest, and stoatworks-backend's
`release/cdpshot.py` (CDPSHOT=path overrides ~/Projects/infrastructure/
stoatworks-backend/release/cdpshot.py). Not run by tools/verify.sh.
(pinhole's crosscheck, with a clock.)

Each case is rendered twice:

  - by the PAGE: demo/ served from here on a free port and loaded in headless
    Chrome on the real GPU (ANGLE on Metal), at ?size=WxH&clip=bars (the colour
    bars stand still, so every frame's input is the same). Start is pressed,
    the page is paused and Mix set to 0: the plugin's final pass then hands
    the clip through untouched, so the canvas IS the input frame, as 8-bit,
    and is read back. Then the case's settings are applied, and the page's
    renderer is told (restartForTest, a hook the page itself never calls) to
    start a fresh plugin instance on its next frame with the clock unit
    settled to seconds through the plugin's own SetClockScaleForTest, as
    bbtest's Rig does before InitGL. The page's host time is then set to
    k/60 for frame k = 0, 1, ... N, one frame at a time, with Pop or New Film
    pressed (1 then 0) before the frame a case names, and the canvas is read
    back at the case's checkpoints. preserveDrawingBuffer is forced on from
    outside so the canvas can be read after its frame; nothing in the page is
    changed for this.
  - by the PLUGIN: `bbtest --pipe --size WxH --set ... [--script cues]` (the
    real BubblePlugin in a GL 4.1 context, its Rig telling SetTime k/60 for
    frame k), fed N + 1 copies of the same input bytes, with the same host
    values: the float32 the page sent, printed to 9 digits; a converted value
    goes through the page's compiled ParamFrom* first. A press is a cue: the
    event 1 at its frame and 0 at the next.

The two outputs are compared byte for byte at each checkpoint (RGB, and alpha
separately). A checkpoint passes at max 1 code and at most 1e-4 of its values
off by any amount. Two negative controls must differ from the page by more
than their case does: bbtest with Thickness one slider step (0.001) from the
page's, and bbtest one frame (1/60 s) later than the page.

Exit 1 when a checkpoint fails, a negative control passes, or the page logs an
error or a WebGL warning.
"""
import argparse
import base64
import functools
import http.server
import json
import os
import subprocess
import sys
import tempfile
import threading
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, ".."))
sys.dont_write_bytecode = True

CDPSHOT = os.environ.get("CDPSHOT", os.path.expanduser("~/Projects/infrastructure/stoatworks-backend/release/cdpshot.py"))
sys.path.insert(0, os.path.dirname(CDPSHOT))
import cdpshot  # noqa: E402

# The real GPU: SwiftShader rounds differently and is slow; --disable-gpu has no WebGL.
cdpshot.GL_FLAGS = ["--use-gl=angle", "--use-angle=metal", "--ignore-gpu-blocklist"]

SIZE = "320x180"
CLIP = "bars"

# name, frames N (frames 0..N are rendered), checkpoints, presses {frame: name},
# settings by the plugin's own names: {'units': x} is in the plugin's units and
# converted by its inverse (ParamFrom*, compiled into the page), an option is
# its element's name, a bare number the host's 0..1.
THIN = {"Thickness": {"units": 0.6}, "Drainage": {"units": 10.0}}
CASES = [
    ("defaults, the first frame", 0, [0], {}, {}),
    ("defaults, stirred, 1 s", 60, [30, 60], {}, {}),
    ("0.6 um at 10 x, Swirl 0, 2 s", 120, [60, 120], {}, {**THIN, "Swirl": {"units": 0.0}}),
    ("Newton at 45 deg, Swirl 0, 2 s", 120, [120], {}, {**THIN, "Swirl": {"units": 0.0}, "Black Film": "Newton", "Angle": {"units": 45.0}}),
    ("stirred at 15 mm/s, 2 s", 120, [60, 120], {}, {"Swirl": {"units": 15.0}}),
    ("Pop at frame 20, 10 frames in", 30, [21, 30], {20: "Pop"}, THIN),
    ("New Film at frame 30", 40, [30, 40], {30: "New Film"}, {}),
    ("Mix 0.5, Exposure 30, 0.5 s", 30, [30], {}, {"Mix": 0.5, "Exposure": {"units": 30.0}}),
]
NEGATIVE_CASE = "0.6 um at 10 x, Swirl 0, 2 s"

PRESERVE = """
(() => {
  const real = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = function (kind, attrs) {
    if (kind === 'webgl2') attrs = Object.assign({}, attrs || {}, { preserveDrawingBuffer: true });
    return real.call(this, kind, attrs);
  };
})();
"""


class Page(cdpshot.Chrome):
    """cdpshot's Chrome, keeping the events it would discard."""

    def __init__(self):
        self.events = []
        super().__init__()
        self.call("Runtime.enable")
        self.call("Log.enable")
        self.call("Page.addScriptToEvaluateOnNewDocument", {"source": PRESERVE})

    def call(self, method, params=None):
        self.msg_id += 1
        want = self.msg_id
        self.ws.send(json.dumps({"id": want, "method": method, "params": params or {}}))
        while True:
            msg = json.loads(self.ws.recv())
            if msg.get("id") == want:
                if "error" in msg:
                    raise RuntimeError(f"{method}: {msg['error']}")
                return msg.get("result", {})
            if "method" in msg:
                self.events.append(msg)

    def run(self, expression):
        r = self.call("Runtime.evaluate", {"expression": expression, "returnByValue": True, "awaitPromise": True})
        if "exceptionDetails" in r:
            raise RuntimeError(f"the page threw: {json.dumps(r['exceptionDetails'])[:400]}")
        return r.get("result", {}).get("value")

    def frames(self):
        return int(self.run("Number(document.querySelector('canvas')?.dataset.frames || 0)"))

    def load(self, url):
        """Load, press Start (nothing is mounted before it), wait for frames."""
        self.call("Page.navigate", {"url": url})
        deadline = time.time() + 30
        started = False
        while time.time() < deadline:
            try:
                if not started and self.run("!!document.getElementById('bb-start') && !document.getElementById('bb-start').hidden"):
                    self.run("document.getElementById('bb-start').click()")
                    started = True
                if started and self.frames() > 2:
                    return
            except RuntimeError:
                pass
            time.sleep(0.25)
        raise RuntimeError(f"{url} drew no frames")

    def after(self, expression):
        """Run, then wait for the frame it causes (the page is paused)."""
        n0 = self.frames()
        self.run(expression)
        deadline = time.time() + 20
        while time.time() < deadline:
            if self.frames() > n0:
                return
            time.sleep(0.01)
        raise RuntimeError("no frame after " + expression[:80])

    def canvas(self):
        """The canvas's bytes, rows top first."""
        r = self.run("""(() => {
          const c = document.querySelector('canvas'); const gl = c.getContext('webgl2');
          const w = c.width, h = c.height, px = new Uint8Array(w * h * 4);
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
          const out = new Uint8Array(w * h * 4);
          for (let y = 0; y < h; y += 1) out.set(px.subarray((h - 1 - y) * w * 4, (h - y) * w * 4), y * w * 4);
          let s = '';
          for (let i = 0; i < out.length; i += 0x8000) s += String.fromCharCode.apply(null, out.subarray(i, i + 0x8000));
          return { w, h, b64: btoa(s) };
        })()""")
        return r["w"], r["h"], base64.b64decode(r["b64"])

    def problems(self):
        out = []
        for e in self.events:
            m, p = e["method"], e.get("params", {})
            if m == "Runtime.exceptionThrown":
                out.append(json.dumps(p.get("exceptionDetails", {}))[:300])
            elif m == "Runtime.consoleAPICalled" and p.get("type") in ("error", "warning", "assert"):
                out.append(" ".join(str(a.get("value", a.get("description", ""))) for a in p.get("args", []))[:300])
            elif m == "Log.entryAdded" and p.get("entry", {}).get("level") in ("error", "warning"):
                out.append(f"{p['entry'].get('source')}: {p['entry'].get('text')}"[:300])
        return out


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve():
    handler = functools.partial(Quiet, directory=DEMO)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}/"


def host_values(page, settings):
    """{name: (kit id, kit value, bbtest value)} from the page's own declarations
    and conversions. An option's element value must be its index: bbtest sends
    the index, the page the element's value."""
    return page.run(f"""import('./plugin.js').then(m => {{
      const out = {{}};
      for (const [name, v] of Object.entries({json.dumps(settings)})) {{
        const p = m.PARAMS.find(x => x.name === name);
        if (!p) throw new Error('no control ' + name);
        if (p.type === 'option') {{
          const i = p.elements.indexOf(v);
          if (i < 0 || p.ff.values[i] !== i) throw new Error(name + ': element ' + v + ' is not at its own index');
          out[name] = [p.id, i, v];
        }} else {{
          const h = Math.fround(typeof v === 'object' ? m.core._bb_param_from(p.ff.index, v.units) : v);
          out[name] = [p.id, h, h];
        }}
      }}
      return out;
    }})""")


def bbtest(binary, size, values, frame, count, presses, override=None, scratch=None):
    """count frames of `frame` through bbtest --pipe; returns the frames' bytes."""
    sets = []
    for name, (_, _, value) in values.items():
        value = (override or {}).get(name, value)
        sets += ["--set", f"{name}={value:.9g}" if isinstance(value, float) else f"{name}={value}"]
    for name, value in (override or {}).items():
        if name not in values:
            sets += ["--set", f"{name}={value:.9g}"]
    script = []
    if presses:
        path = os.path.join(scratch, "cues.txt")
        with open(path, "w") as f:
            for at, name in sorted(presses.items()):
                f.write(f"{at} {name} 1\n{at + 1} {name} 0\n")
        script = ["--script", path]
    r = subprocess.run([binary, "--pipe", "--size", size, *sets, *script], input=frame * count, capture_output=True, timeout=600)
    if r.returncode != 0:
        raise RuntimeError(f"bbtest {' '.join(sets)}: {r.stderr.decode()[:400]}")
    if len(r.stdout) != len(frame) * count:
        raise RuntimeError(f"bbtest wrote {len(r.stdout)} bytes for {count} frames")
    return r.stdout


def compare(ours, theirs, w, h):
    a = np.frombuffer(ours, np.uint8).reshape(h, w, 4).astype(np.int16)
    b = np.frombuffer(theirs, np.uint8).reshape(h, w, 4).astype(np.int16)
    diff = np.abs(a - b)
    return a, b, diff


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--bbtest", required=True)
    ap.add_argument("--keep", help="write each checkpoint's two pictures and their difference (x40) here as PNG")
    args = ap.parse_args()

    w, h = map(int, SIZE.split("x"))
    nbytes = w * h * 4
    server, base = serve()
    page = Page()
    scratch = tempfile.mkdtemp(prefix="bubble-crosscheck-")
    failures = 0
    try:
        page.load(f"{base}?size={SIZE}&clip={CLIP}")
        page.after("document.querySelector('.btn--play').click()")
        page.after("import('./plugin.js').then(m => m.mounted.params.set('mix', 0))")
        cw, ch, frame = page.canvas()
        if (cw, ch) != (w, h):
            raise RuntimeError(f"the canvas is {cw}x{ch}, not {SIZE}")
        indices = page.run("import('./plugin.js').then(m => Object.fromEntries(m.PARAMS.map(p => [p.name, p.ff.index])))")

        for name, n, checkpoints, presses, settings in CASES:
            values = host_values(page, settings)
            kit = {kid: kv for kid, kv, _ in values.values()}
            page.after(f"import('./plugin.js').then(m => m.mounted.params.apply({json.dumps(kit)}))")
            page.after("import('./plugin.js').then(m => { m.renderer.restartForTest(); m.mounted.state.time = 0; m.mounted.redraw(); })")
            ours = {}
            if 0 in checkpoints:
                ours[0] = page.canvas()[2]
            for k in range(1, n + 1):
                press = f"m.renderer.press({indices[presses[k]]});" if k in presses else ""
                page.after(f"import('./plugin.js').then(m => {{ {press} m.mounted.state.time = {k} / 60; m.mounted.redraw(); }})")
                if k in checkpoints:
                    ours[k] = page.canvas()[2]
            theirs = bbtest(args.bbtest, SIZE, values, frame, n + 2, presses, scratch=scratch)
            src = np.frombuffer(frame, np.uint8).reshape(h, w, 4).astype(np.int16)
            for k in checkpoints:
                a, b, diff = compare(ours[k], theirs[k * nbytes:(k + 1) * nbytes], w, h)
                rgb = diff[..., :3]
                off = int((rgb > 0).sum())
                ok = rgb.max() <= 1 and off <= 1e-4 * rgb.size and diff[..., 3].max() <= 1
                failures += not ok
                print(f"{'ok  ' if ok else 'FAIL'}  {name:<34} frame {k:>3}  max {rgb.max()} code, {off} of {rgb.size} values off,"
                      f" alpha max {diff[..., 3].max()}  (the effect moves the clip by {np.abs(b - src)[..., :3].mean():.1f} codes)")
                if args.keep:
                    from PIL import Image
                    os.makedirs(args.keep, exist_ok=True)
                    stem = os.path.join(args.keep, f"{name}-{k}".replace(" ", "-").replace(",", "").replace("/", "-per-"))
                    Image.fromarray(a[..., :3].astype(np.uint8)).save(f"{stem}-page.png")
                    Image.fromarray(b[..., :3].astype(np.uint8)).save(f"{stem}-bbtest.png")
                    Image.fromarray(np.clip(rgb * 40, 0, 255).astype(np.uint8)).save(f"{stem}-diff-x40.png")

            if name == NEGATIVE_CASE:
                k = n
                _, _, own = compare(ours[k], theirs[k * nbytes:(k + 1) * nbytes], w, h)
                own_mean = own[..., :3].mean()
                # One frame later: the same run's next frame.
                _, _, late = compare(ours[k], theirs[(k + 1) * nbytes:(k + 2) * nbytes], w, h)
                # Thickness one slider step from the page's.
                step = float(np.float32(values["Thickness"][2] + 0.001))
                slipped = bbtest(args.bbtest, SIZE, values, frame, n + 1, presses, {"Thickness": step}, scratch)
                _, _, thick = compare(ours[k], slipped[k * nbytes:(k + 1) * nbytes], w, h)
                for label, d in (("negative: one frame later", late), ("negative: Thickness one step off", thick)):
                    caught = d[..., :3].mean() > own_mean
                    failures += not caught
                    print(f"{'ok  ' if caught else 'FAIL'}  {label:<34} frame {k:>3}  max {d[..., :3].max()} code,"
                          f" {int((d[..., :3] > 0).sum())} values off, mean {d[..., :3].mean():.2e} against the case's {own_mean:.2e}")

        problems = page.problems()
        for p in problems:
            print(f"FAIL  the page logged: {p}")
        failures += len(problems)
        renderer = page.run("(() => { const gl = document.createElement('canvas').getContext('webgl2'); const x = gl.getExtension('WEBGL_debug_renderer_info'); return x ? gl.getParameter(x.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER); })()")
        print(f"\nthe page rendered on: {renderer}")
    finally:
        page.close()
        server.shutdown()
    print(f"{'all checkpoints agree' if failures == 0 else f'{failures} failure(s)'}")
    return 1 if failures else 0


if __name__ == "__main__":
    os.environ.setdefault("TMPDIR", tempfile.gettempdir())
    sys.exit(main())
