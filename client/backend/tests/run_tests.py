"""Run the Windows backend and firewall regression executables."""
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
backend = root / "client/backend"
env = os.environ.copy()
if sys.platform != "win32":
    env.setdefault("WINEPREFIX", "/tmp/bigbrother-client-tests-wine")
    env["WINEDEBUG"] = "-all"
    Path(env["WINEPREFIX"]).mkdir(parents=True, exist_ok=True)

if "--lifecycle-only" in sys.argv:
    if sys.platform == "win32":
        raise SystemExit("Run the service lifecycle fixture in an isolated Wine prefix.")
    executable = backend / "build/service-lifecycle-test/BigBrother Client.exe"
    subprocess.run(["wine", str(executable)], cwd=executable.parent, env=env, check=True, timeout=180)
    raise SystemExit(0)

cases = [
    ("build/ipv6-setting-test/test.exe", "--write"),
    ("build/ipv6-setting-test/test.exe", "--read"),
    ("build/token-recovery-test/test.exe",),
    ("build/certificate-state-test/test.exe",),
    ("build/ini-sections-test/test.exe",),
    ("build/secret-permissions-test/test.exe",),
    ("build/notifications-test/test.exe",),
    ("build/notifications-test/test.exe", "--read-disabled"),
    ("build/whitelist-test/BigBrother Client.exe",),
    ("build/policy-test/test.exe",),
    ("build/whitelist-sync-test/test.exe",),
    ("build/hub-test/test.exe",),
]
for case in cases:
    executable = backend / case[0]
    command = [str(executable), *case[1:]]
    if sys.platform != "win32":
        command.insert(0, "wine")
    print(f"Running {case[0]} {' '.join(case[1:])}", flush=True)
    subprocess.run(command, cwd=executable.parent, env=env, check=True, timeout=75)

for name in ("allowlist", "ipv6_block", "whitelist_file", "dns_canary", "backend_lifetime"):
    executable = root / f"firewall/build/{name}_test.exe"
    command = [str(executable)] if sys.platform == "win32" else ["wine", str(executable)]
    subprocess.run(command, cwd=executable.parent, env=env, check=True, timeout=75)
subprocess.run([sys.executable, str(backend / "tests/certificate_probe_test.py")],
               env=env, check=True, timeout=90)
print("Backend, firewall and live TLS regressions passed.", flush=True)
