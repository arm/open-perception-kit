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
from collections.abc import Iterable
from pathlib import Path


LOGGER = logging.getLogger(__name__)
CI_REPOSITORY_ROOT = "/work"


def _is_repository_error(error: ET.Element) -> bool:
    """Return whether any frame in any stack refers to the CI repository."""
    repository_prefix = f"{CI_REPOSITORY_ROOT}/"
    for frame in error.findall(".//stack/frame"):
        for tag in ("obj", "dir", "file"):
            value = frame.findtext(tag)
            if value == CI_REPOSITORY_ROOT or (value and value.startswith(repository_prefix)):
                return True
    return False


def _frame_fingerprint(frame: ET.Element, frame_index: int) -> str:
    """Create a stable fingerprint for a single Valgrind stack frame."""
    parts: list[str] = []
    for tag in ("obj", "fn", "dir", "file"):
        value = frame.findtext(tag)
        if value:
            if tag == "obj":
                library, separator, version = value.rpartition(".so.")
                if separator and all(part.isdigit() for part in version.split(".")):
                    value = f"{library}.so"
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


def _load_errors(path: Path) -> list[ET.Element]:
    """Load error records from a Valgrind XML summary file."""
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

    return root.findall(".//error")


def load_summary(path: Path) -> collections.Counter[str]:
    """Load fingerprint counts from a Valgrind XML summary file."""
    return collections.Counter(
        _error_fingerprint(error)
        for error in _load_errors(path)
    )


def load_repository_summary(path: Path) -> collections.Counter[str]:
    """Load the repository-error fingerprint counts used for comparison."""
    return collections.Counter(
        _error_fingerprint(error)
        for error in _load_errors(path)
        if _is_repository_error(error)
    )


def _group_errors(errors: Iterable[ET.Element]) -> dict[str, list[ET.Element]]:
    """Group error records by the stable fingerprint used for comparison."""
    grouped: dict[str, list[ET.Element]] = collections.defaultdict(list)
    for error in errors:
        grouped[_error_fingerprint(error)].append(error)
    return grouped


def _format_frame(frame: ET.Element) -> str:
    """Format one stack frame with its most useful source location."""
    function = frame.findtext("fn") or "<unknown function>"
    directory = frame.findtext("dir")
    filename = frame.findtext("file")
    line = frame.findtext("line")
    obj = frame.findtext("obj")

    source = filename
    if source and directory:
        source = f"{directory.rstrip('/')}/{source}"
    if source and line:
        source = f"{source}:{line}"

    if source and obj:
        return f"{function} at {source} ({obj})"
    if source:
        return f"{function} at {source}"
    if obj:
        return f"{function} in {obj}"
    return function


def _log_error(status: str, error: ET.Element) -> None:
    """Log one Valgrind error as a readable, source-aware stack trace."""
    LOGGER.info("  [%s] %s", status, error.findtext("kind", default="Unknown"))

    what_text = error.findtext("xwhat/text") or error.findtext("what")
    if what_text:
        LOGGER.info("          %s", what_text)

    stacks = error.findall(".//stack")
    if not stacks:
        LOGGER.info("          Stack trace unavailable")
        return

    for stack_index, stack in enumerate(stacks, start=1):
        if len(stacks) > 1:
            LOGGER.info("          Stack %d:", stack_index)
        for frame_index, frame in enumerate(stack.findall("frame")):
            LOGGER.info("          #%d %s", frame_index, _format_frame(frame))


def _log_errors(
    status: str,
    fingerprints: collections.Counter[str],
    records: dict[str, list[ET.Element]],
) -> None:
    """Log all comparison results with the original XML details."""
    occurrence_by_fingerprint: collections.Counter[str] = collections.Counter()
    for fingerprint in sorted(fingerprints.elements()):
        occurrence = occurrence_by_fingerprint[fingerprint]
        occurrence_by_fingerprint[fingerprint] += 1
        matching_records = records[fingerprint]
        error = matching_records[min(occurrence, len(matching_records) - 1)]
        _log_error(status, error)


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

    baseline_errors = _load_errors(args.baseline)
    current_errors = _load_errors(args.current)
    baseline_errors = [error for error in baseline_errors if _is_repository_error(error)]
    current_errors = [error for error in current_errors if _is_repository_error(error)]
    baseline_records = _group_errors(baseline_errors)
    current_records = _group_errors(current_errors)
    baseline = collections.Counter(map(_error_fingerprint, baseline_errors))
    current = collections.Counter(map(_error_fingerprint, current_errors))

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
        _log_errors("FIXED", fixed_errors, baseline_records)

    if new_errors:
        LOGGER.info("")
        LOGGER.info("New errors (absent in baseline, present in current):")
        _log_errors("NEW", new_errors, current_records)

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
