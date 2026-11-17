"""Render every parameter at both ends of its range and fail if any made no
difference. (ferrofluid's script by way of fountain, filings, intensifier,
convergence and lithophane; the table is bubble's.)

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op
-- so a slider can be stone dead while everything compiles, links, loads and
renders.

Each control is swept where it can act. Every render is 60 frames at 10 fps
(six seconds of film) at 320x180 on the harness's card; nothing is random
and the clock is synthetic, so two renders differ only by what the control
did. Black Film acts only once the film has gone black, so it is swept at
Drainage 10. Slow Motion and Refill act only on a burst, so they are
rendered after 0.5 s: Slow Motion with Burst 0 (the film bursts at once),
Refill after a Pop at Slow Motion 1 (the burst over in 12 ms: Refill 0
shows a fresh film, Refill 10 still nothing). New Film is pressed at frame 30
(--at), so its film is three seconds younger. An option is set here by name.
A control whose ends differ by less than `--floor` (mean 8-bit difference
per channel) is barely alive: vectrix's lesson.

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N] [--bare]

`--bare` drops every per-control context, the addendum's honesty check: what
goes dead there is the list of controls that only act with another one set,
which AGENTS.md records.
"""

import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

# name -> (low settings, high settings, context settings, frames); a list inside is argv as is.
SWEEP = {
    "Thickness": (["Thickness=0"], ["Thickness=1"], [], 60),
    "Drainage": (["Drainage=0"], ["Drainage=1"], [], 60),
    "Swirl": (["Swirl=0"], ["Swirl=1"], [], 60),
    "Black Film": (["Black Film=Common"], ["Black Film=Newton"], ["Drainage=1"], 60),
    "Burst": (["Burst=0"], ["Burst=1"], [], 60),
    "Slow Motion": (["Slow Motion=0"], ["Slow Motion=1"], ["Burst=0"], 5),
    "Refill": (["Refill=0"], ["Refill=1"], ["Pop=1", "Slow Motion=0"], 5),
    "Pop": (["Pop=0"], ["Pop=1"], [], 5),
    "New Film": ([["--at", "30", "New Film=0"]], [["--at", "30", "New Film=1"]], [], 60),
    "Angle": (["Angle=0"], ["Angle=1"], [], 60),
    "Exposure": (["Exposure=0"], ["Exposure=1"], [], 60),
    "Mix": (["Mix=0"], ["Mix=1"], [], 60),
}

# The About block: a text line and browser buttons, with no pixel to sweep.
SKIP = {"About", "Project page", "Source on GitHub", "Support the work", "User guide"}


def read_png(path):
    """Enough of PNG for bbtest's own writer: 8-bit RGBA, filter 0 rows."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, width, height, idat = 8, 0, 0, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
            height = int.from_bytes(body[4:8], "big")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        out += raw[start + 1:start + 1 + stride]
    return bytes(out)


def difference(a, b):
    if len(a) != len(b):
        return 255.0
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


WIDTH, HEIGHT = 320, 180


def render(bbtest, out, settings, frames):
    args = [str(bbtest), "--out", str(out), "--size", f"{WIDTH}x{HEIGHT}", "--frames", str(frames), "--fps", "10"]
    for setting in settings:
        args += list(setting) if isinstance(setting, list) else ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"bbtest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def parameters(bbtest):
    result = subprocess.run([str(bbtest), "--list"], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"bbtest --list failed: {result.stderr.strip()}")
    names = []
    for line in result.stdout.splitlines()[1:]:
        parts = re.split(r"\s{2,}", line.strip())
        if len(parts) >= 3 and parts[0].isdigit():
            names.append(parts[1].strip())
    return names


def sweep_one(bbtest, scratch, name, bare):
    low, high, context, frames = SWEEP[name]
    context = [] if bare else context
    frames = 60 if bare else frames
    tag = f"p{abs(hash(name))}"
    before = render(bbtest, scratch / f"{tag}-a.png", context + low, frames)
    after = render(bbtest, scratch / f"{tag}-b.png", context + high, frames)
    return name, difference(before, after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--bare", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--floor", type=float, default=0.05,
                        help="mean 8-bit difference below which a control is 'barely alive'")
    args = parser.parse_args()

    build = args.build if args.build.is_absolute() else REPO / args.build
    bbtest = build / "bbtest"
    if not bbtest.exists():
        print(f"{bbtest} not found", file=sys.stderr)
        return 1

    plugin = "SW Bubble"
    declared = parameters(bbtest)
    unknown = [n for n in declared if n not in SWEEP and n not in SKIP]
    if unknown:
        # A new parameter with no sweep is a hole, not a pass.
        print(f"{plugin}: no sweep defined for: {', '.join(unknown)}", file=sys.stderr)
        return 1
    dead, weak = [], []
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            names = [n for n in declared if n in SWEEP]
            results = list(pool.map(lambda n: sweep_one(bbtest, scratch, n, args.bare), names))
    for name, delta in results:
        if delta == 0.0:
            dead.append(name)
            print(f"  DEAD {plugin}: {name:16s} both ends identical")
        elif delta < args.floor:
            weak.append(name)
            print(f"  WEAK {plugin}: {name:16s} mean delta {delta:.4f}")
        elif args.verbose:
            print(f"  ok   {plugin}: {name:16s} mean delta {delta:.3f}")
    print(f"{plugin}: {len(results)} parameters swept{' (bare: no context)' if args.bare else ''}, "
          f"{len(dead)} dead, {len(weak)} barely alive")
    if dead or weak:
        if not args.bare:
            print("\nA parameter that changes nothing is usually a uniform name that does not match\n"
                  "the C++, or a setting nothing reads. Both are silent everywhere else.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
