#!/usr/bin/env python3
from pathlib import Path
import re
import sys


VERSION_FILE = Path(__file__).resolve().parents[1] / "VERSION"


def read_version(path=VERSION_FILE):
    version = Path(path).read_text(encoding="utf-8-sig").strip()
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        raise ValueError("VERSION must contain a version such as 2.0.0.")
    if any(int(part) > 65534 for part in version.split(".")):
        raise ValueError("VERSION components must be between 0 and 65534.")
    return version


if __name__ == "__main__":
    try:
        print(read_version())
    except (OSError, ValueError) as error:
        sys.exit(str(error))
