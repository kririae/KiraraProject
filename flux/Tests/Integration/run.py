"""Render FLux fixtures and check analytical values or Mitsuba references."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tomllib

import numpy as np

from compare import compare, read_image, write_image
from reference import FIXTURES, load_cases, read_reference


def run(case: dict, backend: str, binary: Path, output: Path) -> None:
    scene = FIXTURES / case["scene"]
    props = tomllib.loads(scene.read_text())
    reference = None
    if "expected" not in case:
        reference = read_image(read_reference(case, output))
    directory = output / case["id"] / backend
    directory.mkdir(parents=True, exist_ok=True)
    image_path = directory / "render.exr"
    image_path.unlink(missing_ok=True)
    (directory / "metrics.json").unlink(missing_ok=True)
    (directory / "diff.exr").unlink(missing_ok=True)
    log_path = directory / "render.log"
    with log_path.open("w") as log:
        result = subprocess.run(
            [str(binary), str(scene), "--backend", backend, "--spp", str(case["spp"]),
             "--output", str(image_path)],
            stdout=log, stderr=subprocess.STDOUT, text=True, timeout=600,
        )
    if result.returncode:
        raise RuntimeError(f"renderer exited with {result.returncode}; see {directory / 'render.log'}")

    image = read_image(image_path)
    width, height = props["film"]["resolution"]
    if image.shape != (height, width, 3):
        raise ValueError(f"expected {width}x{height} RGB image, got {image.shape}")
    if "expected" in case:
        expected = np.broadcast_to(np.asarray(case["expected"]), image.shape)
    else:
        expected = reference
    metrics, error = compare(image, expected)
    match = re.search(r"in ([0-9.]+) ms, ([0-9.]+) Mpaths/s", log_path.read_text())
    if match:
        metrics["elapsed_ms"] = float(match[1])
        metrics["mpaths_per_second"] = float(match[2])
    write_image(directory / "diff.exr", error)
    (directory / "metrics.json").write_text(json.dumps(metrics, indent=2) + "\n")
    print(f"{case['id']}/{backend}: MAE {metrics['mae']:.6g}, RMSE {metrics['rmse']:.6g}", flush=True)
    if metrics["mae"] > case["mae"] or metrics["rmse"] > case["rmse"]:
        raise ValueError(
            f"error exceeds MAE {case['mae']} or RMSE {case['rmse']}; see {directory}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--flux-bin", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--case", action="append")
    parser.add_argument("--backend", nargs="+", choices=["embree", "optix"], default=["embree", "optix"])
    args = parser.parse_args()
    failed = False
    for case in load_cases(args.case):
        for backend in args.backend:
            try:
                run(case, backend, args.flux_bin.resolve(), args.output_dir.resolve())
            except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
                print(f"FAIL {case['id']}/{backend}: {error}", file=sys.stderr, flush=True)
                failed = True
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
