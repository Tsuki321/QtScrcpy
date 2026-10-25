"""Measure authorized ADB transport, optionally run the Actions-built app.

No taps, application launches on the phone, or screen recordings are performed.
ADB shell round trips include adb process/shell overhead; they are not touch latency.
"""
import argparse
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import time


def summary(values):
    ordered = sorted(values)
    return {"samples": len(values), "median": statistics.median(values),
            "p95": ordered[math.ceil(len(ordered) * .95) - 1],
            "min": min(values), "max": max(values)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--app", type=Path)
    parser.add_argument("--samples", type=int, default=30)
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--preset", choices=["balanced", "low-bandwidth"], default="balanced")
    parser.add_argument("--render-only", action="store_true", help="Start the stream without transport probes")
    args = parser.parse_args()
    if args.samples < 2 or not 5 <= args.seconds <= 600:
        parser.error("Use at least 2 samples and a 5–600 second app run.")
    if args.render_only and not args.app:
        parser.error("--render-only requires --app")
    adb = [str(Path(args.adb).resolve()), "-s", args.serial]

    def run(*command):
        return subprocess.run(adb + list(command), check=True, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=30).stdout

    if run("get-state").strip() != b"device":
        raise RuntimeError("Phone is not connected and authorized")
    model = run("shell", "getprop", "ro.product.model").decode().strip()
    report = {"schema_version": 1, "phone_model": model, "preset": args.preset,
              "scope": "ADB transport and shell/process overhead; not phone-to-screen or input latency."}
    if not args.render_only:
        for _ in range(3):
            run("shell", "true")
        round_trips = []
        for _ in range(args.samples):
            start = time.perf_counter()
            run("shell", "true")
            round_trips.append((time.perf_counter() - start) * 1000)
        throughput = []
        for _ in range(3):
            start = time.perf_counter()
            data = run("exec-out", "head", "-c", "4194304", "/dev/zero")
            elapsed = time.perf_counter() - start
            if len(data) != 4 * 1024 * 1024:
                raise RuntimeError("Incomplete transport sample")
            throughput.append(len(data) * 8 / elapsed / 1e6)
        report["adb_shell_round_trip_ms"] = summary(round_trips)
        report["adb_download_mbps"] = summary(throughput)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.app:
        render_output = args.output.with_name(args.output.stem + "-render.json").resolve()
        env = os.environ.copy()
        env.update(QTSCRCPY_BENCHMARK_SERIAL=args.serial,
                   QTSCRCPY_BENCHMARK_OUTPUT=str(render_output),
                   QTSCRCPY_BENCHMARK_SECONDS=str(args.seconds),
                   QTSCRCPY_BENCHMARK_PRESET=args.preset)
        subprocess.run([str(args.app.resolve())], cwd=args.app.resolve().parent, env=env,
                       timeout=args.seconds + 65, check=True)
        render = json.loads(render_output.read_text(encoding="utf-8"))
        if render["frames"] < 2:
            raise RuntimeError("No usable phone frames received")
        report["render"] = render
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
