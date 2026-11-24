"""Exercise the Windows TLS probe against real, local TLS certificates."""
import hashlib
import os
from pathlib import Path
import ssl
import socket
import subprocess
import sys
import tempfile
import threading
import time

backend = Path(__file__).resolve().parents[1]
exe = backend / "build/certificate-probe-test.exe"
runner = [str(exe)] if sys.platform == "win32" else ["wine", str(exe)]
env = os.environ.copy()
if sys.platform != "win32":
    env.setdefault("WINEPREFIX", "/tmp/bigbrother-review-wine")
    env["WINEDEBUG"] = "-all"
    Path(env["WINEPREFIX"]).mkdir(exist_ok=True)

with tempfile.TemporaryDirectory(prefix="bb-certificate-test-") as directory:
    fixtures = []
    for index in range(2):
        cert = Path(directory) / f"cert-{index}.pem"
        key = Path(directory) / f"key-{index}.pem"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt",
                        "ec_paramgen_curve:P-256", "-nodes", "-keyout", str(key),
                        "-out", str(cert), "-days", "1", "-subj", "/CN=BigBrotherServer"],
                       check=True, capture_output=True)
        der = ssl.PEM_cert_to_DER_cert(cert.read_text())
        fingerprint = hashlib.sha256(der).hexdigest().upper()
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        fixtures.append((context, fingerprint))

    def probe(fixture, expected, accepted, stalled=False):
        context, fingerprint = fixture
        done = threading.Event()
        errors = []
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(15)
            port = listener.getsockname()[1]

            def serve():
                try:
                    conn, _ = listener.accept()
                    with conn:
                        conn.settimeout(12)
                        if stalled:
                            done.wait(12)
                            return
                        with context.wrap_socket(conn, server_side=True) as tls:
                            if tls.recv(1):
                                errors.append("Inspection sent application data")
                except (ssl.SSLEOFError, ConnectionResetError):
                    pass
                except Exception as error:
                    errors.append(str(error))

            thread = threading.Thread(target=serve, daemon=True)
            thread.start()
            start = time.monotonic()
            result = subprocess.run(runner + [str(port), expected], env=env,
                                    capture_output=True, text=True, timeout=15)
            elapsed = time.monotonic() - start
            done.set()
            thread.join(3)
            assert not thread.is_alive() and not errors, errors
            assert result.returncode == (0 if accepted else 1), result.stdout + result.stderr
            if not stalled:
                assert fingerprint in result.stdout, result.stdout + result.stderr
            else:
                assert elapsed < 10, f"Stalled handshake took {elapsed:.1f}s"

    first, second = fixtures
    probe(first, "inspect", True)
    probe(first, first[1], True)
    probe(second, first[1], False)
    probe(second, "inspect", True)
    probe(second, second[1], True)
    probe(second, "inspect", False, stalled=True)
print("Live TLS probe tests passed: inspect, verify, reject replacement, rotate, no credentials, timeout.")
