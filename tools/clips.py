"""Resolume's own demo clips through `bbtest --pipe`: the alpha check, and
the measurement Exposure's default was set from.

    tools/clips.py alpha [--build DIR]      the output alpha on the DXV alpha clips
    tools/clips.py defaults [--build DIR]   each clip's 99th-percentile brightest output
                                            channel over the film's life at Exposure 1 and
                                            at the default: the measurement Exposure's
                                            default was set by

The clips are Resolume Arena's (`/Applications/Resolume Arena/media`), DXV
with alpha, decoded by ffmpeg (`-nostdin`: ffmpeg reads the terminal
otherwise, and a script running it in a loop loses its input). Nothing here
is a second copy of the plugin's formulas: every number is read off the
pipe's frames or its `--stats` lines, which read the output from the float
readback of the plugin's framebuffer. (convergence's script by way of
lithophane; bubble's rule.)

**alpha.** The plugin's decision: output alpha = mix( a, 1, Mix ) -- the
film is an opaque reflection that fills the frame, so at Mix 1 the output is
opaque, and the clip is reflected premultiplied (straight alpha over black: a
transparent pixel reflects nothing). Checked on BattleWeapon_Tank_09 and
Ethnik2, the two clips with real transparency, over 20 frames of a draining
film: at Mix 1 every output alpha is 255; at Mix 0.5 every output alpha is
within half a code of a / 2 + 127.5; and white painted under every fully
transparent pixel changes no output byte, at Mix 1 and at Mix 0.5 (the clips
are straight alpha over black, so without the paint a plugin that ignored
alpha would pass). The negative control paints ~200 opaque pixels instead,
which must show.

**defaults.** Exposure is linear light: the output is Exposure x tint x the
clip's linear light x alpha, and a water film's tint is under 0.08 facing it.
SPEC-bubble.md's rule: at the default film, the median over the demo clips
(`media/AV`, `media/Shop74`) of each clip's 99th-percentile brightest output
channel is 1.0. Measured at 320x180 over 330 frames at 10 fps -- the default
film's life from birth to its burst at about 31 s (and the empty second
after it, which `--stats` leaves out of the pool: only draining frames
count) -- with the clip looped (`-stream_loop -1`) to fill them, the p99
pooled over every pixel of every draining frame, at Exposure 1. There
nothing clips (Exposure 1 x tint < 0.08 x the clip <= 1), so the p99 scales
exactly with Exposure: the default is 1 / the median. This prints the
measurement, re-measures at the binary's default (where the median clip's p99
must be 1.0), and fails if the binary's default is not 1 / the median.
"""

import argparse
import pathlib
import statistics
import subprocess
import sys

import numpy as np

REPO = pathlib.Path(__file__).resolve().parent.parent
MEDIA = pathlib.Path("/Applications/Resolume Arena/media")
ALPHA_CLIPS = ["Shop74/BattleWeapon_Tank_09.mov", "Shop74/Ethnik2.mov"]
EXPOSURE_TOP = 200.0  # Exposure is 0 .. 200, linear (the spec)
LIFE_FRAMES = 330    # at 10 fps: birth to the default film's burst, and past it


def exposure_param(value):
    return value / EXPOSURE_TOP


def decode(path, width, height, frames, start=1.0, loop=False):
    args = ["ffmpeg", "-nostdin", "-v", "error"]
    if loop:
        args += ["-stream_loop", "-1"]
    args += ["-ss", str(start), "-i", str(path), "-frames:v", str(frames),
             "-vf", f"scale={width}:{height}", "-f", "rawvideo", "-pix_fmt", "rgba", "-"]
    raw = subprocess.run(args, capture_output=True, check=True).stdout
    size = width * height * 4
    got = len(raw) // size
    if got == 0:
        raise RuntimeError(f"ffmpeg decoded nothing from {path}")
    return raw[:got * size], got


def pipe(bbtest, raw, width, height, settings, stats=False, fps=10.0):
    args = [str(bbtest), "--pipe", "--size", f"{width}x{height}", "--fps", str(fps)]
    for s in settings:
        args += ["--set", s]
    if stats:
        args += ["--stats"]
    result = subprocess.run(args, input=raw, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(f"bbtest --pipe failed ({result.returncode}): {result.stderr.decode()[-400:]}")
    frames = np.frombuffer(result.stdout, dtype=np.uint8).reshape(-1, height, width, 4)
    pooled, draining = None, 0
    for line in result.stderr.decode().splitlines():
        if line.startswith("frame ") and " draining " in line and "not draining" not in line:
            draining += 1
        if line.startswith("bbtest:") and "over every pixel of every draining frame" in line:
            words = line.split()
            pooled = (float(words[words.index("luminance") + 1]), float(words[words.index("channel") + 1]))
    if stats and pooled is None:
        raise RuntimeError("bbtest --stats printed no pooled p99")
    return frames, pooled, draining


def alpha_check(bbtest):
    width, height, count = 320, 180, 20
    failures, negative_caught = 0, 0
    for name in ALPHA_CLIPS:
        path = MEDIA / name
        if not path.exists():
            print(f"  FAIL  {path} is missing: the alpha check needs Resolume Arena's demo clips")
            return 1
        raw, frames = decode(path, width, height, count)
        clip = np.frombuffer(raw, dtype=np.uint8).reshape(frames, height, width, 4)
        a = clip[..., 3].astype(np.float64)
        full, _, _ = pipe(bbtest, raw, width, height, ["Mix=1"])
        half, _, _ = pipe(bbtest, raw, width, height, ["Mix=0.5"])
        opaque_alpha = int(full[..., 3].min())
        off_half = float(np.abs(half[..., 3].astype(np.float64) - (a / 2.0 + 127.5)).max())
        # The clips are straight alpha over black: where a = 0 the colour is
        # (almost always) black already, so a plugin that ignored alpha would
        # look the same. Paint every fully transparent pixel white: the output
        # must not change by a single byte.
        white = clip.copy()
        hidden = white[..., 3] == 0
        white[..., :3][hidden] = 255
        changed = 0
        for mix, reference in (("Mix=1", full), ("Mix=0.5", half)):
            painted, _, _ = pipe(bbtest, white.tobytes(), width, height, [mix])
            changed += int(np.count_nonzero(painted != reference))
        # The negative control: the same paint on a few opaque pixels must show.
        shown = clip.copy()
        opaque = np.argwhere(shown[..., 3] == 255)
        pick = opaque[:: max(1, len(opaque) // 200)]
        shown[pick[:, 0], pick[:, 1], pick[:, 2], :3] = 255
        control, _, _ = pipe(bbtest, shown.tobytes(), width, height, ["Mix=1"])
        negative_caught += int(np.count_nonzero(control != full)) > 0
        ok = opaque_alpha == 255 and off_half <= 0.5 + 1e-9 and changed == 0
        failures += not ok
        print(f"  {'ok  ' if ok else 'FAIL'}  {path.name}: {frames} frames at {width}x{height}, {hidden.mean():.0%} fully "
              f"transparent; Mix 1: alpha >= {opaque_alpha} (want 255); Mix 0.5: alpha within {off_half:.2f} codes of "
              f"a/2 + 127.5 (tolerance 0.5, the byte's rounding); white painted under every transparent pixel changes "
              f"{changed} output bytes at Mix 1 and 0.5 (want 0)")
    print(f"  {'ok  ' if negative_caught == len(ALPHA_CLIPS) else 'FAIL'}  negative control: white painted on ~200 "
          f"opaque pixels shows in the output on {negative_caught} of {len(ALPHA_CLIPS)} clips")
    failures += negative_caught != len(ALPHA_CLIPS)
    print(f"\n  {'PASS' if failures == 0 else 'FAIL'}")
    return 0 if failures == 0 else 1


def binary_default(bbtest):
    """Exposure's default, read from the binary's own --list (the host's 0..1, four decimals)."""
    listing = subprocess.run([str(bbtest), "--list"], capture_output=True, text=True, check=True).stdout
    for line in listing.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[1] == "Exposure":
            return float(parts[3]) * EXPOSURE_TOP
    raise RuntimeError("no Exposure in bbtest --list")


def defaults(bbtest):
    clips = sorted(p for p in (MEDIA / "AV").glob("*.mov")) + sorted(p for p in (MEDIA / "Shop74").glob("*.mov"))
    if not clips:
        print(f"  FAIL  no demo clips under {MEDIA}")
        return 1
    width, height = 320, 180
    default = binary_default(bbtest)
    at_one, at_default, lum_one = [], [], []
    draining_frames = None
    for path in clips:
        raw, got = decode(path, width, height, LIFE_FRAMES, start=0.0, loop=True)
        if got != LIFE_FRAMES:
            print(f"  FAIL  {path.name}: decoded {got} of {LIFE_FRAMES} frames")
            return 1
        _, (l1, p1), drained = pipe(bbtest, raw, width, height, [f"Exposure={exposure_param(1.0):.7f}"], stats=True)
        _, (_, pd), _ = pipe(bbtest, raw, width, height, [], stats=True)
        draining_frames = drained
        at_one.append(p1)
        at_default.append(pd)
        lum_one.append(l1)
        print(f"  {path.parent.name + '/' + path.name:38s} p99 brightest channel {p1:.6f} at Exposure 1, {pd:.6f} at the "
              f"default (luminance {l1:.6f} at Exposure 1)")
    median = statistics.median(at_one)
    want = 1.0 / median
    print(f"\n{len(clips)} clips, {LIFE_FRAMES} frames at 10 fps ({draining_frames} of them draining), Exposure 1: p99 brightest "
          f"channel {min(at_one):.5f} .. {max(at_one):.5f}, median {median:.6f}, so the rule's Exposure is 1 / {median:.6f} = {want:.4f}")
    reached = statistics.median(at_default)
    print(f"Exposure's default in the binary: {default:.4f}; there the median clip's p99 brightest channel is {reached:.6f} (want 1.0)")
    # --list prints the host's 0..1 to four decimals: Exposure to 200 x 5e-5 = 1e-2.
    # The source writes the default to four decimals: 5e-5. The p99 histogram's bin
    # (2^-20 of the 0..1 range) at Exposure 1, scaled to the default: want^2 x 2^-20.
    tolerance = EXPOSURE_TOP * 5e-5 + 5e-5 + want * want * 2.0 ** -20
    # At the default: the source's four decimals (relative 5e-5 / default), the
    # histogram's bin, and the GPU's encode and the harness's decode (2^-17 each).
    reach_tolerance = 5e-5 / want + want * 2.0 ** -20 + 2.0 ** -20 + 2.0 * 2.0 ** -17
    ok_default = abs(default - want) <= tolerance
    ok_reach = abs(reached - 1.0) <= reach_tolerance
    ok_linear = max(at_one) < 1.0
    print(f"  {'ok  ' if ok_default else 'FAIL'}  the default {default:.4f} against 1 / median {want:.4f}: off by "
          f"{abs(default - want):.5f} (tolerance {tolerance:.5f}: --list's four decimals of the host's 0..1, times {EXPOSURE_TOP:.0f}; "
          f"the source's four decimals; the p99 histogram's bin)")
    print(f"  {'ok  ' if ok_reach else 'FAIL'}  at the default, the median clip's p99 brightest channel {reached:.6f} against "
          f"1: off by {abs(reached - 1.0):.2e} (tolerance {reach_tolerance:.2e}: the source's four decimals, the bins, "
          f"the encode and decode)")
    print(f"  {'ok  ' if ok_linear else 'FAIL'}  every clip under 1 at Exposure 1 (nothing clips, so the p99 scales with "
          f"Exposure): {'yes' if ok_linear else 'NO'}")
    # Clipping makes the check above one-sided: a default too high also reads
    # 1. So the median clip again a thousandth under the default must not.
    middle = clips[sorted(range(len(clips)), key=lambda i: at_one[i])[len(clips) // 2]]
    raw, _ = decode(middle, width, height, LIFE_FRAMES, start=0.0, loop=True)
    _, (_, under), _ = pipe(bbtest, raw, width, height, [f"Exposure={exposure_param(default * 0.999):.7f}"], stats=True)
    ok_under = under < 1.0
    print(f"  {'ok  ' if ok_under else 'FAIL'}  the median clip ({middle.name}) at 0.999 of the default: p99 brightest "
          f"channel {under:.6f} (under 1: the default is the threshold, not past it)")
    ok = ok_default and ok_reach and ok_linear and ok_under
    print(f"\n  {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("what", choices=["alpha", "defaults"])
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    args = parser.parse_args()
    build = args.build if args.build.is_absolute() else REPO / args.build
    bbtest = build / "bbtest"
    if not bbtest.exists():
        print(f"{bbtest} not found", file=sys.stderr)
        return 1
    if args.what == "alpha":
        print("\n=== alpha on a real clip: Resolume's DXV alpha clips through --pipe")
        return alpha_check(bbtest)
    return defaults(bbtest)


if __name__ == "__main__":
    sys.exit(main())
