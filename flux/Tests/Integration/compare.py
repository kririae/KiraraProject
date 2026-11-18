"""Compare linear RGB images without exposure or color adjustments."""

from pathlib import Path

import mitsuba as mi
import numpy as np


def read_image(path: Path) -> np.ndarray:
    bitmap = mi.Bitmap(str(path)).convert(
        mi.Bitmap.PixelFormat.RGB, mi.Struct.Type.Float32, srgb_gamma=False
    )
    image = np.array(bitmap, dtype=np.float64)
    if not np.isfinite(image).all():
        raise ValueError(f"{path}: image contains NaN or infinity")
    return image


def compare(image: np.ndarray, expected: np.ndarray) -> tuple[dict, np.ndarray]:
    if image.shape != expected.shape:
        raise ValueError(f"image shapes differ: {image.shape} vs {expected.shape}")
    if not np.isfinite(image).all() or not np.isfinite(expected).all():
        raise ValueError("image contains NaN or infinity")
    error = np.abs(image - expected)
    return {
        "mae": float(np.mean(error)),
        "rmse": float(np.sqrt(np.mean(error * error))),
        "max_error": float(np.max(error)),
        "mean": image.mean(axis=(0, 1)).tolist(),
    }, error


def write_image(path: Path, image: np.ndarray) -> None:
    mi.Bitmap(np.asarray(image, dtype=np.float32)).write(str(path))
