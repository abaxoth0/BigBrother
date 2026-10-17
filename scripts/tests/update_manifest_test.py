import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("update_manifest", Path(__file__).resolve().parents[1] / "update-manifest.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class UpdateManifestTests(unittest.TestCase):
    def test_select_exact_release_and_parse_sections(self):
        changelog = """# Changelog
## Unreleased
### Added
- Future change
## [1.2.0] - 2026-10-17
### Added
- **Новая функция** with `code` and [documentation](https://example.com).
  Continued description.
  - Nested detail
### Removed
- Old feature
### Changed
- Updated behavior
### Fixed
- Corrected bug
### Security
- Hardened access
## [1.20.0] - 2026-10-01
### Fixed
- Another release
"""
        notes = module.parse_changelog(changelog.replace("\n", "\r\n"), "1.2.0")
        self.assertEqual(notes["version"], "1.2.0")
        self.assertEqual([s["kind"] for s in notes["sections"]], ["added", "removed", "changed", "fixed", "security"])
        self.assertEqual(notes["sections"][0]["items"], ["Новая функция with code and documentation. Continued description.\n  • Nested detail"])
        self.assertNotIn("Another release", json.dumps(notes))
        self.assertEqual(module.parse_changelog(changelog, "Unreleased")["sections"][0]["items"], ["Future change"])

    def test_missing_release_fails(self):
        with self.assertRaisesRegex(ValueError, "not found"):
            module.parse_changelog("## [1.20.0]\n### Fixed\n- Other release", "1.2.0")

    def test_manifest_contains_notes_and_correct_package_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory) / "BigBrother Update.exe"
            package.write_bytes(b"a package fixture")
            notes = module.parse_changelog("## Unreleased\n### Fixed\n- Ошибка исправлена", "Unreleased")
            encoded = module.create_manifest(package, "1.4.0", notes)
            manifest = json.loads(encoded)
            self.assertEqual(manifest["sha256"], hashlib.sha256(package.read_bytes()).hexdigest())
            self.assertEqual(manifest["size"], package.stat().st_size)
            self.assertEqual(manifest["release_notes"], notes)
            self.assertIn("Ошибка исправлена", encoded)
            with self.assertRaisesRegex(ValueError, "too large"):
                module.create_manifest(package, "1.4.0", {"version": "Unreleased", "sections": [{"kind": "fixed", "items": ["a" * module.MAX_METADATA_SIZE]}]})


if __name__ == "__main__":
    unittest.main()
