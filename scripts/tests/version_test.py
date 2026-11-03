import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("version", Path(__file__).resolve().parents[1] / "version.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class VersionTests(unittest.TestCase):
    def test_common_release_version(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "VERSION"
            for value in ("2.0.0", "2.0.0\n", "2.0.0\r\n", "\ufeff2.0.0\n"):
                path.write_text(value, encoding="utf-8")
                self.assertEqual(module.read_version(path), "2.0.0")

    def test_invalid_or_unsupported_versions_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "VERSION"
            for value in ("", "2.0", "2.0.0.0", "2.0.0-beta", "02.0.0", "65535.0.0", "2.0.0\n3.0.0", '2.0.0" -DOTHER'):
                path.write_text(value)
                with self.subTest(version=value), self.assertRaises(ValueError):
                    module.read_version(path)
