#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
"""Collect errors from Valgrind XML log files into one XML output file.

Usage:
	summarize-valgrind-output.py --logs-dir <dir> --output <file>
"""

import argparse
import logging
import posixpath
import re
import xml.etree.ElementTree as ET
from copy import deepcopy
from pathlib import Path


LOGGER = logging.getLogger(__name__)
VALGRIND_XML_GLOB = "*.valgrind.*.xml"
HEX_ADDRESS_RE = re.compile(r"0x[0-9a-fA-F]+")
LOSS_RECORD_RE = re.compile(r"loss record [0-9,]+ of [0-9,]+")
VOLATILE_TEXT_BY_TAG = {
    "ip": "0xADDR",
    "tid": "THREAD",
}
STABLE_HEX_TEXT_TAGS = {
    "unique",
}
FINGERPRINT_IGNORED_TAGS = {
    "ip",
    "tid",
    "unique",
}
FRAME_SYMBOL_TAGS = {
    "fn",
    "file",
    "line",
}
REPO_BUILD_PREFIX = "/work/development/build/meson-out/"
REPO_SOURCE_PREFIX = "/work/development/"
REPO_OWNED_REPORT = "valgrind-repo-owned.md"


def iter_xml_files(logs_dir: Path) -> list[Path]:
    """Return Valgrind XML files found under ``logs_dir`` in deterministic order."""
    return sorted(path for path in logs_dir.rglob(VALGRIND_XML_GLOB) if path.is_file())


def normalize_text(tag: str, text: str | None) -> str | None:
    """Return text with volatile Valgrind values replaced by stable markers."""
    if text is None:
        return None

    stripped = text.strip()
    if not stripped:
        return text

    if tag in VOLATILE_TEXT_BY_TAG:
        return VOLATILE_TEXT_BY_TAG[tag]
    if tag in STABLE_HEX_TEXT_TAGS:
        return stripped

    normalized = LOSS_RECORD_RE.sub("loss record N of N", stripped)
    return HEX_ADDRESS_RE.sub("0xADDR", normalized)


def normalize_error(error: ET.Element) -> ET.Element:
    """Return a copy of a Valgrind error with volatile fields normalized."""
    normalized = deepcopy(error)

    for element in normalized.iter():
        element.text = normalize_text(element.tag, element.text)
        element.tail = normalize_text(element.tag, element.tail)
        for name, value in element.attrib.items():
            element.attrib[name] = HEX_ADDRESS_RE.sub("0xADDR", value)

    for source_frame, normalized_frame in zip(error.iter("frame"), normalized.iter("frame")):
        if frame_has_symbol(source_frame):
            continue

        source_ip = source_frame.findtext("ip")
        normalized_ip = normalized_frame.find("ip")
        if source_ip is not None and normalized_ip is not None:
            normalized_ip.text = source_ip.strip()

    return normalized


def frame_has_symbol(frame: ET.Element) -> bool:
    """Return whether a Valgrind stack frame has stable symbol metadata."""
    return any(frame.find(tag) is not None for tag in FRAME_SYMBOL_TAGS)


def element_fingerprint(element: ET.Element) -> tuple:
    """Return a stable identity tuple for a Valgrind XML element."""
    children = [
        element_fingerprint(child)
        for child in element
        if child.tag not in FINGERPRINT_IGNORED_TAGS
    ]
    if element.tag == "frame" and not frame_has_symbol(element):
        ip = element.findtext("ip")
        if ip is not None:
            children.append(("ip", (), ip.strip(), ()))

    attributes = tuple(sorted(element.attrib.items()))
    text = "" if element.text is None else normalize_text(element.tag, element.text).strip()
    return (element.tag, attributes, text, tuple(children))


def require_complete_valgrind_xml(root: ET.Element, xml_path: Path) -> None:
    """Raise if a Valgrind XML log is not a completed Valgrind run."""
    if root.tag != "valgrindoutput":
        raise ValueError(f"unexpected Valgrind XML root in {xml_path}: {root.tag!r}")

    status_states = [
        state.text.strip()
        for state in root.findall("./status/state")
        if state.text is not None and state.text.strip()
    ]
    if not status_states:
        raise ValueError(f"incomplete Valgrind XML in {xml_path}: no status states found")
    if status_states[-1] != "FINISHED":
        raise ValueError(
            f"incomplete Valgrind XML in {xml_path}: final status is {status_states[-1]!r}"
        )


def is_repo_owned_error(error: ET.Element) -> bool:
    """Return whether an error stack includes repository-owned code."""
    for frame in error.findall(".//frame"):
        directory = frame.findtext("dir") or ""
        obj = frame.findtext("obj") or ""
        if obj.startswith(REPO_BUILD_PREFIX):
            return True
        if directory.startswith(REPO_SOURCE_PREFIX) and "/subprojects/" not in directory:
            return True
    return False


def repo_owned_markdown(root: ET.Element) -> str:
    """Render repository-owned baseline records as human-readable Markdown."""
    errors = [error for error in root.findall("error") if is_repo_owned_error(error)]
    lines = [
        "# Repository-owned Valgrind baseline",
        "",
        f"{len(errors)} baseline record(s) include repository-owned code. "
        "Third-party-only records are excluded.",
        "",
    ]
    for index, error in enumerate(errors, start=1):
        kind = error.findtext("kind", default="Unknown")
        description = error.findtext("xwhat/text") or error.findtext("what") or "No description"
        lines.extend((f"## {index}. {kind}", "", description, ""))
        frames = [
            frame for frame in error.findall(".//frame")
            if (frame.findtext("dir") or "").startswith(REPO_SOURCE_PREFIX)
            and "/subprojects/" not in (frame.findtext("dir") or "")
        ]
        if frames:
            for frame in frames:
                location = posixpath.normpath(posixpath.join(
                    frame.findtext("dir") or "", frame.findtext("file") or ""
                )).removeprefix("/work/")
                if frame.findtext("line"):
                    location += f':{frame.findtext("line")}'
                lines.append(f'- `{location}` — `{frame.findtext("fn") or "<unknown function>"}`')
        else:
            objects = sorted({
                (frame.findtext("obj") or "").removeprefix("/work/development/build/")
                for frame in error.findall(".//frame")
                if (frame.findtext("obj") or "").startswith(REPO_BUILD_PREFIX)
            })
            lines.extend(f"- `{obj}` — source location unavailable" for obj in objects)
        lines.append("")
    return "\n".join(lines)


def collect_errors(logs_dir: Path, output: Path) -> ET.Element:
    """Collect unique normalized Valgrind <error> elements from XML logs."""
    root = ET.Element("valgrindoutput")

    xml_files = [path for path in iter_xml_files(logs_dir) if path.resolve() != output.resolve()]

    total_logs = 0
    total_errors = 0
    duplicate_errors = 0
    seen_errors = set()

    for xml_path in xml_files:
        try:
            tree = ET.parse(xml_path)
        except FileNotFoundError:
            continue
        except ET.ParseError as exc:
            raise ValueError(f"malformed XML in {xml_path}: {exc}") from exc

        source_root = tree.getroot()
        require_complete_valgrind_xml(source_root, xml_path)
        total_logs += 1
        errors = list(source_root.findall(".//error"))
        if not errors and source_root.tag != "error":
            continue

        for error in errors or [source_root]:
            total_errors += 1
            fingerprint = element_fingerprint(error)
            if fingerprint in seen_errors:
                duplicate_errors += 1
                continue

            seen_errors.add(fingerprint)
            root.append(normalize_error(error))

    root.set("source_logs", str(total_logs))
    root.set("raw_errors", str(total_errors))
    root.set("duplicate_errors", str(duplicate_errors))
    root.set("collected_errors", str(len(root)))
    root.set("repo_owned_errors", str(sum(map(is_repo_owned_error, root))))
    return root


def write_xml(root: ET.Element, output: Path) -> None:
    """Write a prettified XML document to ``output``."""
    tree = ET.ElementTree(root)
    ET.indent(tree, space="  ")
    output.parent.mkdir(parents=True, exist_ok=True)
    tree.write(output, encoding="utf-8", xml_declaration=True)


def write_markdown(root: ET.Element, output: Path) -> None:
    output.write_text(repo_owned_markdown(root), encoding="utf-8")


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--logs-dir",
        required=True,
        type=Path,
        help="Directory containing Valgrind XML log files",
    )
    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="Path to the XML file that will receive the collected errors",
    )
    args = parser.parse_args()

    logs_dir = args.logs_dir
    output = args.output

    if not logs_dir.exists():
        LOGGER.error("Error: logs directory does not exist: %s", logs_dir)
        return 2
    if not logs_dir.is_dir():
        LOGGER.error("Error: logs path is not a directory: %s", logs_dir)
        return 2

    try:
        root = collect_errors(logs_dir, output)
    except ValueError as exc:
        LOGGER.error("Error: %s", exc)
        return 2

    write_xml(root, output)
    write_markdown(root, output.with_name(REPO_OWNED_REPORT))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
