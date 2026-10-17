#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import re


KINDS = {"added", "removed", "changed", "fixed", "deprecated", "security"}
MAX_METADATA_SIZE = 128 * 1024


def plain_text(text):
    text = re.sub(r"\[([^\]]+)\]\([^\n]+?\)", r"\1", text)
    text = re.sub(r"`+([^`]+)`+", r"\1", text)
    return text.replace("**", "").strip()


def parse_changelog(text, version):
    selected = False
    found = False
    sections = []
    section = None
    for line in text.splitlines():
        heading = re.match(r"^##\s+(?:\[([^\]]+)\]|(\S+))", line)
        if heading:
            if selected:
                break
            selected = (heading[1] or heading[2]).casefold() == version.casefold()
            found |= selected
            continue
        if not selected:
            continue
        heading = re.match(r"^###\s+(.+?)\s*$", line)
        if heading:
            kind = heading[1].casefold()
            if kind not in KINDS:
                raise ValueError(f"Unsupported changelog section: {heading[1]}")
            section = next((s for s in sections if s["kind"] == kind), None)
            if section is None:
                section = {"kind": kind, "items": []}
                sections.append(section)
            continue
        if section is None or not line.strip():
            continue
        bullet = re.match(r"^(\s*)[-*+]\s+(.+)$", line)
        if bullet:
            item = plain_text(bullet[2])
            if bullet[1] and section["items"]:
                section["items"][-1] += "\n  • " + item
            else:
                section["items"].append(item)
        elif line[:1].isspace() and section["items"]:
            section["items"][-1] += " " + plain_text(line)
    if not found:
        raise ValueError(f"Changelog version {version!r} was not found; add it or select --notes-version Unreleased.")
    return {"version": version, "sections": [s for s in sections if s["items"]]}


def select_notes(text, version, selector=None):
    if selector and selector != "auto":
        return parse_changelog(text, selector)
    headings = {match[1] or match[2] for match in re.finditer(r"^##\s+(?:\[([^\]]+)\]|(\S+))", text, re.MULTILINE)}
    return parse_changelog(text, version if version in headings else "Unreleased")


def create_manifest(package, version, notes):
    digest = hashlib.sha256()
    with package.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    manifest = {"version": version, "file": package.name, "size": package.stat().st_size,
                "sha256": digest.hexdigest(), "release_notes": notes}
    encoded = json.dumps(manifest, ensure_ascii=False, separators=(",", ":"))
    if len(encoded.encode("utf-8")) >= MAX_METADATA_SIZE - 16384:
        raise ValueError("Release notes are too large for the update manifest.")
    return encoded


def main():
    parser = argparse.ArgumentParser(description="Build client update metadata with a selected CHANGELOG.md release.")
    parser.add_argument("--package", type=Path)
    parser.add_argument("--version")
    parser.add_argument("--changelog", type=Path, required=True)
    parser.add_argument("--notes-version")
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()
    try:
        from version import read_version
        version = read_version()
        if args.version and args.version != version:
            raise ValueError(f"Package version must match VERSION ({version}).")
        notes = select_notes(args.changelog.read_text(encoding="utf-8-sig"), version, args.notes_version)
        if args.validate_only:
            return
        if args.package is None:
            parser.error("--package is required unless --validate-only is used")
        manifest = create_manifest(args.package, version, notes)
        args.package.with_suffix(".json").write_text(manifest + "\n", encoding="utf-8")
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
