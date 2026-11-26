import importlib.util
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("compile_commands", ROOT / "scripts/compile_commands.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CompileCommandTests(unittest.TestCase):
    def test_multi_source_recipe_preserves_quoted_defines_and_include_paths(self):
        directory = Path("/tmp/native project")
        arguments = [
            "x86_64-w64-mingw32-g++", '-DBB_VERSION="2.3.4"', "-Iinclude directory",
            "-std=c++20", "-DDEBUG", "-o", "build/Test Application.exe",
            "tests/main.cpp", "src/client.cpp", "build/log.o", "build/notify.a",
            "-lws2_32", "-static-libgcc",
        ]
        entries = list(module.parse_commands(shlex.join(arguments), directory))
        self.assertEqual(len(entries), 2)
        for entry, source in zip(entries, ("tests/main.cpp", "src/client.cpp")):
            self.assertEqual(entry["file"], str(directory / source))
            self.assertEqual(entry["arguments"], [
                *arguments[:5], "-c", str(directory / source),
            ])

    def test_link_and_resource_recipes_do_not_become_source_entries(self):
        output = "\n".join([
            "x86_64-w64-mingw32-windres -i app.rc -o build/app.o",
            "x86_64-w64-mingw32-g++ -o app.exe build/main.o -lws2_32",
            "true -C ../notifications debug",
        ])
        self.assertEqual(list(module.parse_commands(output, ROOT)), [])


@unittest.skipUnless(
    all(shutil.which(tool) for tool in ("make", *module.COMPILERS)),
    "Native configuration integration tests require Make and MinGW",
)
class NativeConfigurationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run([sys.executable, str(ROOT / "scripts/compile_commands.py")],
                       cwd="/tmp", check=True, capture_output=True, text=True)
        cls.commands = json.loads((ROOT / "build/compile_commands.json").read_text())

    def test_database_covers_native_sources_with_build_version_and_language(self):
        entries = {Path(entry["file"]): entry for entry in self.commands}
        self.assertEqual(len(entries), len(self.commands))
        version = (ROOT / "VERSION").read_text().strip()
        for directory in (
            "app/client/backend",
            "app/process-monitor",
            "app/firewall",
            "app/notifications",
            "lib/common",
        ):
            for source in (ROOT / directory).rglob("*"):
                if source.suffix not in (".c", ".cpp") or {"build", "third-party"} & set(source.parts):
                    continue
                with self.subTest(source=source.relative_to(ROOT)):
                    flags = entries[source]["arguments"]
                    self.assertIn(f'-DBB_VERSION="{version}"', flags)
                    self.assertIn("--target=x86_64-w64-mingw32", flags)
                    self.assertTrue(Path(flags[0]).is_absolute())
                    if source.suffix == ".cpp":
                        standard = "c++17" if directory == "app/notifications" else "c++20"
                        self.assertIn(f"-std={standard}", flags)

        self.assertFalse(any(path.is_relative_to(ROOT / "lib/mbedtls") for path in entries))

    def test_backend_and_monitor_build_without_each_others_private_headers(self):
        backend = ROOT / "app/client/backend"
        monitor = ROOT / "app/process-monitor"
        for entry in self.commands:
            source = Path(entry["file"])
            if source.is_relative_to(backend / "src"):
                other_app = monitor
            elif source.is_relative_to(monitor / "src"):
                other_app = backend
            else:
                continue
            with self.subTest(source=source.relative_to(ROOT)):
                # MinGW already targets Windows; --target is added for clangd only.
                arguments = [
                    arg for arg in entry["arguments"] if not arg.startswith("--target=")
                ]
                arguments[arguments.index("-c")] = "-MM"
                result = subprocess.run(
                    arguments,
                    cwd=entry["directory"],
                    capture_output=True,
                    text=True,
                    check=True,
                )
                dependencies = shlex.split(result.stdout.replace("\\\n", " "))[1:]
                for path in dependencies:
                    resolved = (Path(entry["directory"]) / path).resolve()
                    self.assertFalse(resolved.is_relative_to(other_app), resolved)

    @unittest.skipUnless(shutil.which("clangd"), "clangd is required for editor smoke tests")
    def test_clangd_reports_no_errors_in_each_module_and_shared_headers(self):
        for source in (
            "app/client/backend/src/ipc_client.cpp", "app/client/backend/src/tls_client.cpp",
            "app/firewall/src/firewall.c", "app/notifications/src/toast.cpp",
            "app/notifications/include/report.h", "lib/common/version.h", "lib/common/log/log.c",
        ):
            with self.subTest(source=source):
                # Exercise diagnostics without clangd's unrelated refactoring self-tests.
                result = subprocess.run(
                    ["clangd", f"--check={ROOT / source}", "--tweaks=none"],
                    cwd="/tmp", capture_output=True, text=True, timeout=60,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("All checks completed, 0 errors", result.stderr)
