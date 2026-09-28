#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Collect errors from Valgrind XML log files into one XML output file.

Usage:
	summarize-valgrind-output.py --logs-dir <dir> --output <file> [--suppressions-file <file>]
"""

import argparse
import logging
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


def start_suppression_block(block: list[str] | None, path: Path) -> list[str]:
    if block is not None:
        raise ValueError(f"nested suppression block in {path}")
    return []


def finish_suppression_block(
    block: list[str] | None, names: set[str], path: Path
) -> None:
    if not block:
        raise ValueError(f"empty or unmatched suppression block in {path}")
    name = block[0]
    if name in names:
        raise ValueError(f"duplicate suppression name in {path}: {name}")
    names.add(name)


def read_suppression_names(path: Path) -> set[str]:
    """Return the unique suppression names from a Valgrind suppression file."""
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise ValueError(f"cannot read suppression file {path}: {exc}") from exc

    names = set()
    block = None
    for raw_line in lines:
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line == "{":
            block = start_suppression_block(block, path)
        elif line == "}":
            finish_suppression_block(block, names, path)
            block = None
        elif block is None:
            raise ValueError(f"content outside suppression block in {path}: {line}")
        else:
            block.append(line)

    if block is not None:
        raise ValueError(f"unterminated suppression block in {path}")
    if not names:
        raise ValueError(f"no suppression blocks found in {path}")
    return names


def read_valgrind_xml(xml_path: Path) -> ET.Element | None:
    try:
        tree = ET.parse(xml_path)
    except FileNotFoundError:
        return None
    except ET.ParseError as exc:
        raise ValueError(f"malformed XML in {xml_path}: {exc}") from exc
    root = tree.getroot()
    require_complete_valgrind_xml(root, xml_path)
    return root


def record_used_suppressions(root: ET.Element, used_suppressions: set[str]) -> None:
    for pair in root.findall(".//suppcounts/pair"):
        name = pair.findtext("name", default="").strip()
        count = int(pair.findtext("count", default="0"))
        if name and count > 0:
            used_suppressions.add(name)


def append_unique_errors(
    destination: ET.Element, source: ET.Element, seen_errors: set[tuple]
) -> tuple[int, int]:
    errors = list(source.findall(".//error"))
    if not errors and source.tag == "error":
        errors = [source]
    duplicate_errors = 0
    for error in errors:
        fingerprint = element_fingerprint(error)
        if fingerprint in seen_errors:
            duplicate_errors += 1
            continue
        seen_errors.add(fingerprint)
        destination.append(normalize_error(error))
    return len(errors), duplicate_errors


def collect_errors(
    logs_dir: Path,
    output: Path,
    used_suppressions: set[str] | None = None,
) -> ET.Element:
    """Collect unique normalized Valgrind <error> elements from XML logs."""
    root = ET.Element("valgrindoutput")

    xml_files = [path for path in iter_xml_files(logs_dir) if path.resolve() != output.resolve()]

    total_logs = 0
    total_errors = 0
    duplicate_errors = 0
    seen_errors = set()

    for xml_path in xml_files:
        source_root = read_valgrind_xml(xml_path)
        if source_root is None:
            continue
        if used_suppressions is not None:
            record_used_suppressions(source_root, used_suppressions)
        total_logs += 1
        errors, duplicates = append_unique_errors(root, source_root, seen_errors)
        total_errors += errors
        duplicate_errors += duplicates

    root.set("source_logs", str(total_logs))
    root.set("raw_errors", str(total_errors))
    root.set("duplicate_errors", str(duplicate_errors))
    root.set("collected_errors", str(len(root)))
    return root


def write_xml(root: ET.Element, output: Path) -> None:
    """Write a prettified XML document to ``output``."""
    tree = ET.ElementTree(root)
    ET.indent(tree, space="  ")
    output.parent.mkdir(parents=True, exist_ok=True)
    tree.write(output, encoding="utf-8", xml_declaration=True)


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
    parser.add_argument(
        "--suppressions-file",
        type=Path,
        help="Fail if any suppression in this file was unused by every Valgrind log",
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

    used_suppressions = set()
    try:
        expected_suppressions = (
            read_suppression_names(args.suppressions_file)
            if args.suppressions_file is not None
            else set()
        )
        root = collect_errors(logs_dir, output, used_suppressions)
    except ValueError as exc:
        LOGGER.error("Error: %s", exc)
        return 2

    write_xml(root, output)
    unused_suppressions = sorted(expected_suppressions - used_suppressions)
    if unused_suppressions:
        LOGGER.error(
            "FAILED: %d unused Valgrind suppression(s):",
            len(unused_suppressions),
        )
        for name in unused_suppressions:
            LOGGER.error("  %s", name)
        LOGGER.error("Remove unused suppressions from %s.", args.suppressions_file)
        return 1
    if expected_suppressions:
        LOGGER.info(
            "PASSED: All %d Valgrind suppressions were used.",
            len(expected_suppressions),
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
