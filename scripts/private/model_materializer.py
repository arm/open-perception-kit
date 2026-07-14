#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Materialize published model files required by PEK OpChains."""

from __future__ import annotations

import argparse
from collections import Counter
import json
import logging
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Any, Sequence


DEFAULT_WORKSPACE_ROOT = Path("/work")
DEFAULT_MODELFETCH_EXECUTABLE = Path("/opt/pek-venvs/model-tools/bin/modelfetch")
MODEL_STORE_RELATIVE_PATH = Path("var/models")
LOGGER = logging.getLogger("model-materializer")


class MaterializationError(RuntimeError):
    """Raised when model discovery or materialization cannot complete safely."""


def _load_json_object(path: Path, description: str) -> dict[str, Any]:
    try:
        parsed = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise MaterializationError(f"Cannot read {description} {path}: {exc}") from exc
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise MaterializationError(
            f"Invalid JSON in {description} {path}: {exc}"
        ) from exc

    if not isinstance(parsed, dict):
        raise MaterializationError(f"Expected a JSON object in {description} {path}")
    return parsed


def _require_workspace_path(path: Path, workspace_root: Path, description: str) -> Path:
    try:
        resolved = path.resolve()
    except (OSError, RuntimeError) as exc:
        raise MaterializationError(
            f"Cannot resolve {description} {path}: {exc}"
        ) from exc
    if not resolved.is_relative_to(workspace_root):
        raise MaterializationError(
            f"{description} escapes workspace root {workspace_root}: {path}"
        )
    return resolved


def _resolve_workspace_reference(
    value: str, workspace_root: Path, description: str
) -> Path:
    reference = Path(value)
    if reference.is_absolute():
        try:
            relative = reference.relative_to(DEFAULT_WORKSPACE_ROOT)
        except ValueError:
            candidate = reference
        else:
            candidate = workspace_root / relative
    else:
        candidate = workspace_root / reference
    return _require_workspace_path(candidate, workspace_root, description)


def _descriptor_references(opchain_path: Path, workspace_root: Path) -> list[Path]:
    opchain = _load_json_object(opchain_path, "OpChain")
    operations = opchain.get("ops")
    if not isinstance(operations, list):
        raise MaterializationError(
            f"OpChain {opchain_path} must contain an 'ops' array"
        )

    references: set[Path] = set()
    for index, operation in enumerate(operations):
        if not isinstance(operation, dict):
            raise MaterializationError(
                f"OpChain {opchain_path} operation {index} is not an object"
            )
        attributes = operation.get("attributes", {})
        if not isinstance(attributes, dict):
            raise MaterializationError(
                f"OpChain {opchain_path} operation {index} attributes are not an object"
            )
        value = attributes.get("modelDescriptor")
        if value is None:
            continue
        if not isinstance(value, str) or not value:
            raise MaterializationError(
                f"OpChain {opchain_path} operation {index} has an invalid modelDescriptor"
            )
        references.add(
            _resolve_workspace_reference(value, workspace_root, "Model descriptor")
        )
    return sorted(references)


def collect_download_requirements(
    opchain_paths: Sequence[Path], workspace_root: Path
) -> list[str]:
    workspace_root = workspace_root.resolve()
    if not workspace_root.is_dir():
        raise MaterializationError(
            f"Workspace root is not a directory: {workspace_root}"
        )
    models_root = (workspace_root / "config/models").resolve()
    if not models_root.is_dir():
        raise MaterializationError(
            f"Model configuration directory is missing: {models_root}"
        )

    resolved_opchains = {
        _resolve_workspace_reference(str(path), workspace_root, "OpChain path")
        for path in opchain_paths
    }
    descriptor_paths: set[Path] = set()
    for opchain_path in sorted(resolved_opchains):
        if not opchain_path.is_file():
            raise MaterializationError(f"OpChain file does not exist: {opchain_path}")
        descriptor_paths.update(_descriptor_references(opchain_path, workspace_root))

    asset_ids: set[str] = set()
    for descriptor_path in sorted(descriptor_paths):
        if not descriptor_path.is_relative_to(models_root):
            raise MaterializationError(
                f"Model descriptor must be below {models_root}: {descriptor_path}"
            )
        if not descriptor_path.is_file():
            raise MaterializationError(
                f"Model descriptor does not exist: {descriptor_path}"
            )
        descriptor = _load_json_object(descriptor_path, "model descriptor")
        model_file = descriptor.get("modelFile")
        if not isinstance(model_file, str) or not model_file:
            raise MaterializationError(
                f"Model descriptor {descriptor_path} has no non-empty modelFile"
            )
        if not model_file.startswith("hf:"):
            local_reference = Path(model_file)
            if local_reference.is_absolute() or ".." in local_reference.parts:
                raise MaterializationError(
                    f"Local modelFile in {descriptor_path} is unsafe"
                )
            local_model = _require_workspace_path(
                descriptor_path.parent / local_reference,
                workspace_root,
                "Local model file",
            )
            if not local_model.is_relative_to(descriptor_path.parent):
                raise MaterializationError(
                    f"Local modelFile in {descriptor_path} escapes its model directory"
                )
            if not local_model.is_file():
                raise MaterializationError(
                    f"Local model file does not exist: {local_model}"
                )
            continue
        if "#file=" not in model_file:
            raise MaterializationError(
                f"Published modelFile in {descriptor_path} must identify one file asset"
            )
        asset_ids.add(model_file)
    return sorted(asset_ids)


def _parse_results(stdout: str) -> list[dict[str, Any]]:
    try:
        document = json.loads(stdout)
    except json.JSONDecodeError as exc:
        raise MaterializationError("modelfetch returned invalid JSON") from exc
    if not isinstance(document, dict) or set(document) != {"results"}:
        raise MaterializationError(
            "modelfetch response must contain exactly one results field"
        )
    results = document["results"]
    if not isinstance(results, list) or not all(
        isinstance(item, dict) for item in results
    ):
        raise MaterializationError(
            "modelfetch response contains an invalid results array"
        )
    return results


def _validate_results(
    results: Sequence[dict[str, Any]],
    asset_ids: Sequence[str],
) -> Counter[str]:
    if len(results) != len(asset_ids):
        raise MaterializationError("modelfetch result count does not match the request")

    statuses: Counter[str] = Counter()
    failure_reasons: list[str] = []
    for result, asset_id in zip(results, asset_ids, strict=True):
        if result.get("asset_id") != asset_id:
            raise MaterializationError("modelfetch results are not in request order")
        status = result.get("status")
        if not isinstance(status, str):
            raise MaterializationError("modelfetch returned a result without a status")
        if status in {"downloaded", "existing"}:
            if set(result) != {"asset_id", "status", "paths", "integrity"}:
                raise MaterializationError(
                    "modelfetch returned an invalid success result"
                )
        elif status == "failed":
            if set(result) != {"asset_id", "status", "reason"}:
                raise MaterializationError(
                    "modelfetch returned an invalid failure result"
                )
            reason = result.get("reason")
            if not isinstance(reason, str) or not reason:
                raise MaterializationError(
                    "modelfetch returned a failure without a reason"
                )
            failure_reasons.append(reason)
        else:
            raise MaterializationError("modelfetch returned an unknown status")
        statuses[status] += 1

    if failure_reasons:
        detail = "; ".join(failure_reasons)
        if "snapshot_unavailable" in failure_reasons:
            detail += (
                "; verify network access and Hugging Face credentials "
                "(HF_TOKEN or HF_TOKEN_PATH)"
            )
        raise MaterializationError(f"modelfetch failed: {detail}")
    return statuses


def run_modelfetch(
    asset_ids: Sequence[str],
    *,
    executable: Path,
    workspace_root: Path,
) -> Counter[str]:
    if not asset_ids:
        return Counter()
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise MaterializationError(f"modelfetch executable is missing: {executable}")

    try:
        model_store_root = (workspace_root / MODEL_STORE_RELATIVE_PATH).resolve()
    except (OSError, RuntimeError) as exc:
        raise MaterializationError(f"Cannot resolve the model store: {exc}") from exc
    if not model_store_root.is_relative_to(workspace_root):
        raise MaterializationError(
            f"Model store escapes workspace root: {model_store_root}"
        )
    unique_asset_ids = sorted(set(asset_ids))
    requests = [
        {"asset_id": asset_id, "destination": str(model_store_root)}
        for asset_id in unique_asset_ids
    ]

    request_file: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            prefix="pek-model-download-",
            suffix=".json",
            delete=False,
        ) as handle:
            request_file = Path(handle.name)
            json.dump({"downloads": requests}, handle, separators=(",", ":"))
            handle.write("\n")

        try:
            completed = subprocess.run(
                [str(executable), "models", "download-request", str(request_file)],
                cwd=workspace_root,
                check=False,
                capture_output=True,
                encoding="utf-8",
                errors="strict",
            )
        except OSError as exc:
            raise MaterializationError(f"Cannot start modelfetch: {exc}") from exc
        except UnicodeError as exc:
            raise MaterializationError("modelfetch returned non-UTF-8 output") from exc
    finally:
        if request_file is not None:
            request_file.unlink(missing_ok=True)

    stdout = completed.stdout.strip()
    if not stdout:
        detail = completed.stderr.strip() or f"exit code {completed.returncode}"
        raise MaterializationError(f"modelfetch produced no result: {detail}")
    statuses = _validate_results(_parse_results(stdout), unique_asset_ids)
    if completed.returncode != 0:
        detail = completed.stderr.strip() or f"exit code {completed.returncode}"
        raise MaterializationError(
            f"modelfetch failed after successful results: {detail}"
        )
    return statuses


def materialize_opchains(
    opchain_paths: Sequence[Path],
    *,
    workspace_root: Path = DEFAULT_WORKSPACE_ROOT,
    modelfetch_executable: Path = DEFAULT_MODELFETCH_EXECUTABLE,
) -> Counter[str]:
    workspace_root = workspace_root.resolve()
    asset_ids = collect_download_requirements(opchain_paths, workspace_root)
    statuses = run_modelfetch(
        asset_ids,
        executable=modelfetch_executable,
        workspace_root=workspace_root,
    )
    LOGGER.info(
        "materialized %d published model(s); downloaded=%d existing=%d",
        len(asset_ids),
        statuses["downloaded"],
        statuses["existing"],
    )
    return statuses


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Materialize published models required by PEK OpChains."
    )
    parser.add_argument(
        "--opchain",
        action="append",
        type=Path,
        required=True,
        help="Expanded OpChain path; repeat for every pekinfer element",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="[model-materializer] %(message)s")
    args = parse_args(argv)
    try:
        materialize_opchains(args.opchain)
    except MaterializationError as exc:
        LOGGER.error("%s", exc)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
