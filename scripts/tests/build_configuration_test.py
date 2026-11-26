import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class BuildConfigurationTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("dotnet"), "requires the .NET SDK")
    def test_frontend_profiles_publish_windows_executables_to_installer_payload(self):
        for product in ("Client", "Server"):
            with self.subTest(product=product):
                side = product.lower()
                project = ROOT / "app" / side / "frontend" / f"BigBrother {product}.csproj"
                output = subprocess.check_output([
                    "dotnet", "msbuild", str(project), "-p:PublishProfile=FolderProfile",
                    "-getProperty:PublishDir,PublishUrl,RuntimeIdentifier,SelfContained,PublishSingleFile",
                ], text=True)
                properties = json.loads(output)["Properties"]
                expected = ROOT / "dist" / f"{side}-frontend"
                for key in ("PublishDir", "PublishUrl"):
                    self.assertEqual(Path(properties[key]).resolve(), expected)
                self.assertEqual(properties["RuntimeIdentifier"], "win-x64")
                self.assertEqual(properties["SelfContained"], "true")
                self.assertEqual(properties["PublishSingleFile"], "true")

    def test_installers_reject_missing_or_mismatched_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "build-configuration.txt"
            for configuration in (None, "", "Debug\n"):
                if configuration is not None:
                    marker.write_text(configuration)
                with self.subTest(configuration=configuration):
                    result = subprocess.run(
                        ["bash", str(ROOT / "scripts/build-installers.sh"), "Release", directory],
                        cwd="/tmp", capture_output=True, text=True,
                    )
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertIn("Payload configuration does not match Release", result.stderr)

    def test_build_scripts_require_an_explicit_configuration(self):
        for script in ("build.sh", "publish.sh", "build-installers.sh"):
            for arguments in ([], ["debug"], ["release"], ["/tmp/payload"], ["Unknown"]):
                with self.subTest(script=script, arguments=arguments):
                    result = subprocess.run(
                        ["bash", str(ROOT / "scripts" / script), *arguments],
                        cwd="/tmp", capture_output=True, text=True,
                    )
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertIn("Debug or Release", result.stderr)

    def test_help_does_not_require_build_dependencies(self):
        for script in ("build.sh", "publish.sh", "build-installers.sh"):
            with self.subTest(script=script):
                result = subprocess.run(
                    ["bash", str(ROOT / "scripts" / script), "--help"],
                    cwd="/tmp", capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("<Debug|Release>", result.stdout)

    def test_extra_arguments_are_rejected_before_building(self):
        for script, count in (("build.sh", 3), ("publish.sh", 3), ("build-installers.sh", 5)):
            with self.subTest(script=script):
                result = subprocess.run(
                    ["bash", str(ROOT / "scripts" / script), "Release", *(["unused"] * (count - 1))],
                    cwd="/tmp", capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn("Too many arguments", result.stderr)
