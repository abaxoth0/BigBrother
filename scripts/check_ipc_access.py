# -*- coding: utf-8 -*-
"""Probe whether another process can access BigBrother's local IPC named pipes.

Run from an ordinary (non-elevated) shell:     py -3 check_ipc_access.py
Run as an ELEVATED shell (like the real GUI):  py -3 check_ipc_access.py

What to expect after the IPC hardening (Phase 2):
  * Non-elevated process: every pipe open should be DENIED (DACL grants only
    SYSTEM / Administrators / Owner).
  * Elevated process: firewall + server-frontend pipes may OPEN (Admins are
    granted), but the client-backend pipe should OPEN-then-SILENTLY-CLOSE /
    NOT respond, because that pipe also verifies the peer executable name
    ("BigBrother Client.exe"). A different exe (python.exe) is rejected.
"""

import ctypes
import sys
import time
from ctypes import wintypes

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

# Explicit prototypes: required on 64-bit Python so HANDLE/pointers aren't
# truncated to 32-bit.
LPSECURITY_ATTRIBUTES = ctypes.c_void_p
LPOVERLAPPED = ctypes.c_void_p
kernel32.CreateFileW.restype = wintypes.HANDLE
kernel32.CreateFileW.argtypes = [
    wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
    LPSECURITY_ATTRIBUTES, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
]
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL
kernel32.WriteFile.argtypes = [
    wintypes.HANDLE, ctypes.c_char_p, wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD), LPOVERLAPPED,
]
kernel32.WriteFile.restype = wintypes.BOOL
kernel32.ReadFile.argtypes = [
    wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD), LPOVERLAPPED,
]
kernel32.ReadFile.restype = wintypes.BOOL
kernel32.PeekNamedPipe.argtypes = [
    wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
    ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p,
]
kernel32.PeekNamedPipe.restype = wintypes.BOOL

GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

PIPES = {
    "server frontend  (BigBrother.Server.Frontend)": r"\\.\pipe\BigBrother.Server.Frontend",
    "client backend  (BigBrother.Client.Backend)": r"\\.\pipe\BigBrother.Client.Backend",
    "firewall daemon (BigBrother.Firewall)": r"\\.\pipe\BigBrother.Firewall",
}
PAYLOAD = {
    r"\\.\pipe\BigBrother.Server.Frontend": "PING\n\n",
    r"\\.\pipe\BigBrother.Client.Backend": "GET_STATUS\n\n",
    r"\\.\pipe\BigBrother.Firewall": "PING\n",
}


def is_elevated():
    try:
        return ctypes.windll.shell32.IsUserAnAdmin() != 0
    except Exception:
        return False


def open_pipe(name):
    """Returns handle if opened, else error code string."""
    h = kernel32.CreateFileW(
        name,
        GENERIC_READ | GENERIC_WRITE,
        0,
        None,
        OPEN_EXISTING,
        0,
        None,
    )
    if h == INVALID_HANDLE_VALUE:
        return None, ctypes.get_last_error()
    return h, None


def probe(handle, payload, timeout=1.5):
    n = wintypes.DWORD()
    data = payload.encode("utf-8")
    if not kernel32.WriteFile(handle, data, len(data), ctypes.byref(n), None):
        return f"write failed rc={ctypes.get_last_error()}"

    t0 = time.time()
    while time.time() - t0 < timeout:
        avail = wintypes.DWORD(0)
        ok = kernel32.PeekNamedPipe(handle, None, 0, None, ctypes.byref(avail), None)
        if not ok:
            return f"closed/no response rc={ctypes.get_last_error()}"
        if avail.value > 0:
            buf = ctypes.create_string_buffer(avail.value)
            got = wintypes.DWORD(0)
            if kernel32.ReadFile(handle, buf, avail.value, ctypes.byref(got), None):
                return repr(buf.raw[: got.value])
        time.sleep(0.05)
    return "no response in time"


def main():
    print("=" * 70)
    print("BigBrother IPC access probe")
    print(f"Running elevated: {is_elevated()}")
    print("=" * 70)

    all_ok = True
    for label, name in PIPES.items():
        handle, err = open_pipe(name)
        if handle is None:
            if err == 2:  # ERROR_FILE_NOT_FOUND -> pipe server not running
                print(f"[{label}] pipe not present rc={err} (server/daemon not running?)")
            elif err == 5:  # ERROR_ACCESS_DENIED
                print(f"[{label}] OPEN DENIED rc=5 (blocked)"
                      " -> SECURE")
            else:
                print(f"[{label}] open failed rc={err}")
            continue

        resp = probe(handle, PAYLOAD[name])
        kernel32.CloseHandle(handle)
        if resp.startswith("closed") or resp.startswith("no response"):
            print(f"[{label}] opened but got NO response ({resp})"
                  " -> BLOCKED (client-backend peer check)")
        else:
            print(f"[{label}] opened and got a response: {resp}")
            all_ok = False

    print("=" * 70)
    if all_ok:
        print("No unexpected access observed.")
    else:
        print("Some pipes responded to this probe process.")
        print("  client-backend is the critical one; it should never answer "
              "an arbitrary exe. Firewall/server-frontend respond to ADMIN "
              "processes by design (DACL grants Administrators).")
    print("Notes:")
    print("  * Run un-elevated to confirm DACL blocks normal users.")
    print("  * Run as admin to confirm the client-backend pipe still rejects "
          "python.exe (peer check) while the other two remain admin-accessible.")
    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()