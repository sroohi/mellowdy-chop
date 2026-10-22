#!/usr/bin/env python3
"""Artifact-integrity tests; no PyTorch or downloaded model is required."""

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "script" / "prepare_stem_model.py"
SPEC = importlib.util.spec_from_file_location("prepare_stem_model", SCRIPT)
HELPER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HELPER)


class ModelPreparationIntegrityTests(unittest.TestCase):
    def test_corrupt_checkpoint_rejected_before_loading_ml_dependencies(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "bad.th"
            path.write_bytes(b"invalid checkpoint")
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                HELPER.verify_checkpoint(path)

    def test_corrupt_native_artifact_is_preserved_and_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "model.bin"
            content = b"invalid converted weights"
            path.write_bytes(content)
            result = subprocess.run(
                [sys.executable, str(SCRIPT), "--output", str(path), "--verify-only"],
                capture_output=True, text=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("checksum mismatch", result.stderr)
            self.assertEqual(path.read_bytes(), content)

    def test_verify_only_does_not_create_or_download_a_missing_model(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "model.bin"
            result = subprocess.run(
                [sys.executable, str(SCRIPT), "--output", str(path), "--verify-only"],
                capture_output=True, text=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Native model missing", result.stderr)
            self.assertEqual(list(Path(folder).iterdir()), [])


if __name__ == "__main__":
    unittest.main()
