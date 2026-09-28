"""Generate Mitsuba references from the XML paired with each FLux scene."""

import argparse
import hashlib
import json
from pathlib import Path
import tomllib
import xml.etree.ElementTree as ET

import mitsuba as mi

from compare import write_image

ROOT = Path(__file__).resolve().parent
FIXTURES = ROOT.parent / "Fixtures"


def load_cases(ids: list[str] | None = None) -> list[dict]:
    cases = tomllib.loads((ROOT / "Cases.toml").read_text())["case"]
    if ids:
        unknown = set(ids) - {case["id"] for case in cases}
        if unknown:
            raise ValueError(f"unknown cases: {', '.join(sorted(unknown))}")
        cases = [case for case in cases if case["id"] in ids]
    return cases


def recipe(case: dict, variant: str, spp: int, seed: int) -> dict:
    xml = FIXTURES / case["reference"]
    paths = {xml, FIXTURES / case["scene"]}
    for node in ET.parse(xml).iter("string"):
        if node.get("name") == "filename":
            paths.add((xml.parent / node.attrib["value"]).resolve())

    # Include FLux-only inputs too, so a change to either scene requires review.
    def collect(value):
        if isinstance(value, dict):
            for key, child in value.items():
                if key == "path" and isinstance(child, str):
                    paths.add((FIXTURES / case["scene"]).parent.joinpath(child).resolve())
                else:
                    collect(child)
        elif isinstance(value, list):
            for child in value:
                collect(child)

    collect(tomllib.loads((FIXTURES / case["scene"]).read_text()))
    return {
        "variant": variant,
        "spp": spp,
        "seed": seed,
        "mitsuba": mi.__version__,
        "uv_lock": hashlib.sha256((ROOT / "uv.lock").read_bytes()).hexdigest(),
        "generator": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "inputs": {
            str(path.relative_to(FIXTURES)): hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(paths)
        },
    }


def read_reference(case: dict, output: Path):
    directory = output / case["id"]
    if not (directory / "reference.json").exists() or not (directory / "reference.exr").exists():
        raise ValueError(f"{case['id']}: missing reference; run reference.py first")
    metadata = json.loads((directory / "reference.json").read_text())
    expected = recipe(case, metadata["variant"], metadata["spp"], metadata["seed"])
    if metadata != expected:
        raise ValueError(f"{case['id']}: reference inputs changed; rerun reference.py")
    return directory / "reference.exr"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--case", action="append")
    parser.add_argument("--variant", choices=["llvm_ad_rgb", "cuda_ad_rgb"], default="llvm_ad_rgb")
    parser.add_argument("--spp", type=int, default=4096)
    parser.add_argument("--seed", type=int, default=17)
    parser.add_argument(
        "--stale", action="store_true", help="skip references whose recorded inputs still match"
    )
    args = parser.parse_args()
    if args.spp <= 0:
        parser.error("--spp must be positive")
    mi.set_variant(args.variant)
    for case in load_cases(args.case):
        directory = args.output_dir / case["id"]
        directory.mkdir(parents=True, exist_ok=True)
        metadata_path = directory / "reference.json"
        metadata = recipe(case, args.variant, args.spp, args.seed)
        if (
            args.stale
            and metadata_path.exists()
            and (directory / "reference.exr").exists()
            and json.loads(metadata_path.read_text()) == metadata
        ):
            print(f"{case['id']}: reference is current", flush=True)
            continue
        # Failed regeneration leaves no valid reference metadata.
        metadata_path.unlink(missing_ok=True)
        scene = mi.load_file(str(FIXTURES / case["reference"]))
        image = mi.render(scene, spp=args.spp, seed=args.seed)
        write_image(directory / "reference.exr", image)
        metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
        print(f"{case['id']}: wrote reference ({args.spp} spp, {args.variant})", flush=True)


if __name__ == "__main__":
    main()
