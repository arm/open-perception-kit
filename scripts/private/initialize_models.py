#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Download every published model asset declared by PEK before runtime."""

from __future__ import annotations

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
LOGGER = logging.getLogger("model-initializer")


class ModelInitializationError(RuntimeError):
    """Raised when model discovery or download cannot complete safely."""


def _load_json_object(path: Path) -> dict[str, Any]:
    try:
        parsed = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise ModelInitializationError(
            f"Cannot read model config {path}: {exc}"
        ) from exc
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ModelInitializationError(
            f"Invalid JSON in model config {path}: {exc}"
        ) from exc

    if not isinstance(parsed, dict):
        raise ModelInitializationError(f"Expected a JSON object in model config {path}")
    return parsed


def collect_asset_ids(workspace_root: Path) -> list[str]:
    workspace_root = workspace_root.resolve()
    models_root = (workspace_root / "config/models").resolve()
    if not models_root.is_relative_to(workspace_root) or not models_root.is_dir():
        raise ModelInitializationError(
            f"Model configuration directory is missing or unsafe: {models_root}"
        )

    asset_ids: set[str] = set()
    for config_path in sorted(models_root.rglob("*.json")):
        try:
            resolved_path = config_path.resolve(strict=True)
        except (OSError, RuntimeError) as exc:
            raise ModelInitializationError(
                f"Cannot resolve model config {config_path}: {exc}"
            ) from exc
        if not resolved_path.is_relative_to(models_root) or not resolved_path.is_file():
            raise ModelInitializationError(
                f"Model config escapes the configuration directory: {config_path}"
            )

        config = _load_json_object(resolved_path)
        if "modelFile" not in config:
            continue
        model_file = config["modelFile"]
        if not isinstance(model_file, str) or not model_file:
            raise ModelInitializationError(
                f"Model config {config_path} has an invalid modelFile"
            )
        if model_file.startswith("hf:"):
            if "#file=" not in model_file:
                raise ModelInitializationError(
                    f"Published model config {config_path} must identify one file asset"
                )
            asset_ids.add(model_file)

    return sorted(asset_ids)


def download_assets(
    asset_ids: Sequence[str],
    *,
    executable: Path,
    workspace_root: Path,
) -> None:
    if not asset_ids:
        return
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise ModelInitializationError(
            f"modelfetch executable is missing: {executable}"
        )

    workspace_root = workspace_root.resolve()
    model_store_root = (workspace_root / MODEL_STORE_RELATIVE_PATH).resolve()
    if not model_store_root.is_relative_to(workspace_root):
        raise ModelInitializationError(
            f"Model store escapes workspace root: {model_store_root}"
        )

    downloads = [
        {"asset_id": asset_id, "destination": str(model_store_root)}
        for asset_id in sorted(set(asset_ids))
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
            json.dump({"downloads": downloads}, handle, separators=(",", ":"))
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
            raise ModelInitializationError(f"Cannot start modelfetch: {exc}") from exc
        except UnicodeError as exc:
            raise ModelInitializationError(
                "modelfetch returned non-UTF-8 output"
            ) from exc
    finally:
        if request_file is not None:
            request_file.unlink(missing_ok=True)

    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        if not detail:
            detail = f"exit code {completed.returncode}"
        raise ModelInitializationError(f"modelfetch failed: {detail}")


def initialize_models(
    *,
    workspace_root: Path = DEFAULT_WORKSPACE_ROOT,
    modelfetch_executable: Path = DEFAULT_MODELFETCH_EXECUTABLE,
) -> None:
    workspace_root = workspace_root.resolve()
    asset_ids = collect_asset_ids(workspace_root)
    download_assets(
        asset_ids,
        executable=modelfetch_executable,
        workspace_root=workspace_root,
    )
    LOGGER.info("initialized %d published model asset(s)", len(asset_ids))


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="[model-initializer] %(message)s")
    try:
        initialize_models()
    except ModelInitializationError as exc:
        LOGGER.error("%s", exc)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
