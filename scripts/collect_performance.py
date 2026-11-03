"""Collect native Release measurements with build provenance; no speed gates."""
import argparse
import json
import os
import platform
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("binary", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
run = subprocess.run([str(args.binary.resolve())], cwd=root, text=True, encoding="utf-8", capture_output=True, timeout=180, check=True)
report = json.loads(run.stdout)
if report["build_type"] != "Release":
    raise SystemExit("Collect performance artifacts only from a Release build")
report.update({"commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
               "platform": platform.platform(), "machine": platform.machine(), "logical_cpus": os.cpu_count(),
               "runner_image": os.environ.get("ImageVersion", "local"),
               "interpretation": "Synthetic same-run samples; CPU render time is not display FPS. Compare only equivalent hardware, compiler and fixtures."})
cache = args.binary.resolve().parent / "CMakeCache.txt"
if not cache.exists():
    cache = cache.parent.parent / "CMakeCache.txt"
report["cmake_toolchain"] = [line for line in cache.read_text(encoding="utf-8").splitlines() if line.startswith(("CMAKE_CXX_COMPILER:", "CMAKE_CXX_FLAGS", "CMAKE_BUILD_TYPE:"))]
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
print(f"Wrote {len(report['measurements'])} measurement series to {args.output}")
