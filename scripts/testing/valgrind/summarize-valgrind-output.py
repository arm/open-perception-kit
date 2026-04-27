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
import xml.etree.ElementTree as ET
from copy import deepcopy
from pathlib import Path


LOGGER = logging.getLogger(__name__)
VALGRIND_XML_GLOB = "*.valgrind.*.xml"


def iter_xml_files(logs_dir: Path) -> list[Path]:
    """Return Valgrind XML files found under ``logs_dir`` in deterministic order."""
    return sorted(path for path in logs_dir.rglob(VALGRIND_XML_GLOB) if path.is_file())


def collect_errors(logs_dir: Path, output: Path) -> ET.Element:
    """Collect all Valgrind <error> elements from XML logs."""
    root = ET.Element("valgrindoutput")

    xml_files = [path for path in iter_xml_files(logs_dir) if path.resolve() != output.resolve()]

    total_logs = 0
    total_errors = 0

    for xml_path in xml_files:
        try:
            tree = ET.parse(xml_path)
        except FileNotFoundError:
            continue
        except ET.ParseError as exc:
            raise ValueError(f"malformed XML in {xml_path}: {exc}") from exc

        source_root = tree.getroot()
        errors = list(source_root.findall(".//error"))
        if not errors and source_root.tag != "error":
            continue

        total_logs += 1
        for error in errors or [source_root]:
            root.append(deepcopy(error))
            total_errors += 1

    root.set("source_logs", str(total_logs))
    root.set("collected_errors", str(total_errors))
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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
