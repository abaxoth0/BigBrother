#!/usr/bin/env python3
"""Check first-party native files, types and callable functions for Doxygen documentation."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]


def documented(element):
    return any(
        node is not None and "".join(node.itertext()).strip()
        for node in (element.find("briefdescription"), element.find("detaileddescription"))
    )


def audit(xml_directory):
    missing = []
    seen = set()
    functions = 0
    for path in sorted(xml_directory.glob("*.xml")):
        tree = ET.parse(path)
        for compound in tree.findall("compounddef"):
            if compound.get("kind") in ("file", "class", "struct", "union") and not documented(compound):
                missing.append(f"{compound.get('kind')}: {compound.findtext('compoundname')}")

        for member in tree.findall(".//memberdef[@kind='function']"):
            identifier = member.get("id")
            arguments = member.findtext("argsstring", "")
            if identifier in seen or re.search(r"=\s*delete\b", arguments):
                continue
            seen.add(identifier)
            functions += 1
            if not documented(member):
                location = member.find("location")
                missing.append(
                    f"{location.get('file')}:{location.get('line')}: {member.findtext('definition')}"
                )

    if not functions:
        raise ValueError("Doxygen produced no callable entries; documentation coverage was not checked")
    return functions, sorted(missing)


def main():
    with tempfile.TemporaryDirectory(prefix="bigbrother-native-docs-") as directory:
        config = (ROOT / "Doxyfile").read_text() + f'''
OUTPUT_DIRECTORY = "{directory}"
EXTRACT_ALL = YES
EXTRACT_PRIVATE = YES
GENERATE_XML = YES
GENERATE_HTML = NO
'''
        subprocess.run(["doxygen", "-"], input=config, text=True, cwd=ROOT, check=True)
        functions, missing = audit(Path(directory) / "xml")

    if missing:
        print("Missing Doxygen documentation:\n" + "\n".join(missing), file=sys.stderr)
        return 1
    print(f"Native documentation covers all files/types and {functions} callable entries (excluding deleted functions).")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, ET.ParseError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
