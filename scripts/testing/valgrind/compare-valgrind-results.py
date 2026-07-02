#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
"""Compare two Valgrind XML error summaries.

Exits 0 when there are no new errors relative to the baseline.
Exits 1 when new errors are detected.
Exits 2 when there is an error reading or parsing the summary files.

Usage:
    compare-valgrind-results.py --baseline <file> --current <file>
"""

import argparse
import collections
import logging
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


LOGGER = logging.getLogger(__name__)


def _frame_fingerprint(frame: ET.Element, frame_index: int) -> str:
    """Create a stable fingerprint for a single Valgrind stack frame."""
    parts: list[str] = []
    for tag in ("obj", "fn", "dir", "file"):
        value = frame.findtext(tag)
        if value:
            parts.append(f"{tag}={value}")

    if parts:
        return ";".join(parts)

    return f"unsymbolized[{frame_index}]"


def _error_fingerprint(error: ET.Element) -> str:
    """Create a stable fingerprint for one Valgrind error record.

    The fingerprint intentionally ignores line numbers so source-only line
    churn inside an otherwise unchanged file does not look like a new error.
    """
    parts: list[str] = [f"kind={error.findtext('kind', default='')}"]

    what_text = error.findtext("xwhat/text") or error.findtext("what")
    if what_text:
        parts.append(f"what={what_text}")

    frames = [
        _frame_fingerprint(frame, frame_index)
        for frame_index, frame in enumerate(error.findall(".//stack/frame"))
    ]
    if frames:
        parts.append("stack=" + "|".join(frames))

    return "||".join(parts)


def load_summary(path: Path) -> collections.Counter[str]:
    """Load fingerprint counts from a Valgrind XML summary file."""
    try:
        data = path.read_text(encoding="utf-8")
    except OSError as exc:
        LOGGER.error("Error: cannot read %s: %s", path, exc)
        sys.exit(2)

    try:
        root = ET.fromstring(data)
    except ET.ParseError as exc:
        LOGGER.error("Error: malformed XML in %s: %s", path, exc)
        sys.exit(2)

    if root.tag != "valgrindoutput":
        LOGGER.error(
            "Error: unexpected XML root element in %s: %s",
            path,
            root.tag,
        )
        sys.exit(2)

    return collections.Counter(
        _error_fingerprint(error)
        for error in root.findall(".//error")
    )


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(message)s")

    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--baseline",
        required=True,
        type=Path,
        help="Baseline Valgrind XML summary produced on the target branch",
    )
    parser.add_argument(
        "--current",
        required=True,
        type=Path,
        help="Current Valgrind XML summary produced for this branch/PR",
    )
    args = parser.parse_args()

    baseline = load_summary(args.baseline)
    current = load_summary(args.current)

    new_errors = current - baseline
    fixed_errors = baseline - current

    baseline_count = sum(baseline.values())
    current_count = sum(current.values())
    new_error_count = sum(new_errors.values())
    fixed_error_count = sum(fixed_errors.values())

    LOGGER.info("Baseline errors : %d", baseline_count)
    LOGGER.info("Current errors  : %d", current_count)
    LOGGER.info("New errors      : %d", new_error_count)
    LOGGER.info("Fixed errors    : %d", fixed_error_count)

    if fixed_errors:
        LOGGER.info("")
        LOGGER.info("Fixed errors (present in baseline, absent in current):")
        for err in sorted(fixed_errors.elements()):
            kind, *frames = err.split("||")
            LOGGER.info("  [FIXED] %s", kind)
            for frame in frames:
                LOGGER.info("            %s", frame)

    if new_errors:
        LOGGER.info("")
        LOGGER.info("New errors (absent in baseline, present in current):")
        for err in sorted(new_errors.elements()):
            kind, *frames = err.split("||")
            LOGGER.info("  [NEW] %s", kind)
            for frame in frames:
                LOGGER.info("          %s", frame)

        LOGGER.error(
            ""
        )
        LOGGER.error(
            "FAILED: %d new Valgrind error(s) introduced compared to the baseline.",
            new_error_count,
        )
        sys.exit(1)

    LOGGER.info("")
    LOGGER.info("PASSED: No new Valgrind errors compared to the baseline.")


if __name__ == "__main__":
    main()
