#!/usr/bin/env python3
"""Prepare the pinned native HTDemucs model. Python is a build dependency only.

Conversion follows sevagh/demucs.cpp's convert-pth-to-ggml.py at the pinned
commit below, without constructing the PyTorch architecture or contacting a
model registry. The checkpoint and final native bytes are both SHA-256 pinned.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
from urllib.request import urlopen


CHECKPOINT_URL = "https://dl.fbaipublicfiles.com/demucs/hybrid_transformer/955717e8-8726e21a.th"
CHECKPOINT_BYTES = 84_141_911
CHECKPOINT_SHA256 = "8726e21a993978c7ba086d3872e7608d7d5bfca646ca4aca459ffda844faa8b4"
DEMUCS_CPP_COMMIT = "f1206e9adeea103aef4a636b9e62297cf1f8e34e"
MODEL_SHA256 = "72b17c42d308982ddb5069bc3bf48b81a5aac4cb6516e4366c0fa7cef6df0064"
MODEL_NAME = "ggml-model-htdemucs-4s-f16.bin"
MODEL_TENSORS = 533
MAGIC = 0x646D6334


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_checkpoint(path: Path) -> None:
    if path.stat().st_size != CHECKPOINT_BYTES or sha256(path) != CHECKPOINT_SHA256:
        raise ValueError(f"Checkpoint checksum mismatch: {path}. Expected {CHECKPOINT_SHA256}.")


def download_checkpoint(path: Path) -> None:
    if path.exists():
        verify_checkpoint(path)
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(prefix="htdemucs-", suffix=".partial", dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            size = 0
            # urllib preserves TLS certificate validation; there is no insecure mode.
            with urlopen(CHECKPOINT_URL, timeout=60) as response:
                for block in iter(lambda: response.read(1024 * 1024), b""):
                    size += len(block)
                    if size > CHECKPOINT_BYTES:
                        raise ValueError("Checkpoint response exceeds the pinned artifact size.")
                    stream.write(block)
        verify_checkpoint(temporary)
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def convert_checkpoint(checkpoint: Path, output: Path) -> str:
    verify_checkpoint(checkpoint)
    try:
        import numpy as np
        import torch
        from fractions import Fraction
    except ImportError as error:
        raise RuntimeError(
            "Model conversion needs build-time torch==2.6.0 and numpy==2.2.6. "
            "Install CPU PyTorch on Linux (https://download.pytorch.org/whl/cpu); "
            "the shipped app does not use Python."
        ) from error

    class ArchitectureMetadata:
        """Inert stand-in for a class reference stored as checkpoint metadata."""

    # This restricted loader does not import Demucs or execute its architecture.
    # The only extra globals in this hash-pinned checkpoint are inert architecture
    # metadata, Fraction and NumPy scalar/dtype constructors used by training args.
    safe_globals = [
        (ArchitectureMetadata, "demucs.htdemucs.HTDemucs"),
        Fraction,
        (np._core.multiarray.scalar, "numpy.core.multiarray.scalar"),
        np.dtype,
        type(np.dtype(np.float64)),
        type(np.dtype(np.float32)),
    ]
    with torch.serialization.safe_globals(safe_globals):
        saved = torch.load(checkpoint, map_location="cpu", weights_only=True)
    state = saved.get("state")
    if not isinstance(state, dict) or len(state) != MODEL_TENSORS:
        raise ValueError("Unexpected checkpoint tensor dictionary.")

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(prefix="htdemucs-", suffix=".partial", dir=output.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(struct.pack("<I", MAGIC))
            for name, tensor in state.items():
                if not isinstance(name, str) or not isinstance(tensor, torch.Tensor) or tensor.dtype != torch.float16:
                    raise ValueError("Unexpected tensor name or dtype in the pinned checkpoint.")
                value = tensor.detach().cpu().squeeze().contiguous().numpy()
                if not np.isfinite(value).all():
                    raise ValueError(f"Nonfinite model weights: {name}")
                name_bytes = name.encode("utf-8")
                stream.write(struct.pack("<ii", value.ndim, len(name_bytes)))
                for dimension in value.shape:
                    stream.write(struct.pack("<i", dimension))
                stream.write(name_bytes)
                stream.write(value.astype("<f2", copy=False).tobytes(order="C"))
        digest = sha256(temporary)
        if digest != MODEL_SHA256:
            raise ValueError(f"Native model checksum mismatch: expected {MODEL_SHA256}, got {digest}.")
        os.replace(temporary, output)
        temporary = None
        return digest
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="native model destination")
    parser.add_argument("--checkpoint", type=Path, help="existing hash-verified official checkpoint")
    parser.add_argument("--verify-only", action="store_true", help="verify existing native model without Python ML dependencies")
    args = parser.parse_args()
    if not MODEL_SHA256:
        raise RuntimeError("The native output checksum has not been pinned.")
    output = args.output.resolve()
    if output.exists():
        if sha256(output) != MODEL_SHA256:
            raise ValueError(f"Existing native model checksum mismatch: {output}. Remove or replace this artifact.")
        print(f"Verified native four-stem model: {output}\nSHA256 {MODEL_SHA256}")
        return 0
    if args.verify_only:
        raise FileNotFoundError(f"Native model missing: {output}")
    checkpoint = args.checkpoint.resolve() if args.checkpoint else output.parent / ".source-checkpoint" / "955717e8-8726e21a.th"
    if args.checkpoint:
        verify_checkpoint(checkpoint)
    else:
        print("Downloading hash-pinned official Meta HTDemucs checkpoint...", flush=True)
        download_checkpoint(checkpoint)
    print("Converting to the pinned native demucs.cpp format...", flush=True)
    digest = convert_checkpoint(checkpoint, output)
    provenance = {
        "model": "HTDemucs four-source float16",
        "license": "MIT",
        "checkpoint_url": CHECKPOINT_URL,
        "checkpoint_sha256": CHECKPOINT_SHA256,
        "native_sha256": digest,
        "demucs_cpp_commit": DEMUCS_CPP_COMMIT,
        "tensor_count": MODEL_TENSORS,
        "runtime": "native C++ (no Python)",
    }
    output.with_suffix(output.suffix + ".json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(f"Prepared {output.stat().st_size:,} bytes: {output}\nSHA256 {digest}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"Model preparation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
