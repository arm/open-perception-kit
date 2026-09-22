#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import argparse
import hashlib
import json
import logging
import os
import sys
from pathlib import Path

from huggingface_hub import hf_hub_download
from huggingface_hub.constants import HF_HUB_CACHE
from jsonschema import Draft202012Validator
from jsonschema.exceptions import SchemaError, ValidationError
from referencing.exceptions import Unresolvable

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from config_versions import check_version  # noqa: E402

LOGGER = logging.getLogger(__name__)
MODEL_SCHEMA = (
    Path(__file__).resolve().parents[1] / "config/schemas/v1/model.schema.json"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download model artifacts declared by OPK model descriptors.",
    )
    parser.add_argument(
        "--models-dir",
        required=True,
        type=Path,
        help="Directory recursively searched for JSON model descriptors.",
    )
    args = parser.parse_args()
    if not args.models_dir.is_dir():
        parser.error(f"--models-dir is not a directory: {args.models_dir}")
    return args


def _cache_dir(token: str | None) -> Path:
    # Keep each credential from reading artifacts cached by another auth context.
    namespace = hashlib.sha256(token.encode()).hexdigest() if token else "anonymous"
    return Path(HF_HUB_CACHE) / namespace


def _load_validator() -> Draft202012Validator:
    try:
        schema = json.loads(MODEL_SCHEMA.read_text())
        Draft202012Validator.check_schema(schema)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"Invalid model schema {MODEL_SCHEMA}: {error}") from error
    except SchemaError as error:
        raise ValueError(
            f"Invalid model schema {MODEL_SCHEMA}: {error.message}"
        ) from error
    return Draft202012Validator(schema)


def _prepare_download(descriptor: Path, validator: Draft202012Validator):
    try:
        model = json.loads(descriptor.read_text())
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"Invalid model descriptor {descriptor}: {error}") from error
    if not isinstance(model, dict) or not {"modelFile", "hfDownload"} & model.keys():
        return None

    try:
        validator.validate(model)
    except ValidationError as error:
        raise ValueError(
            f"Invalid model descriptor {descriptor}: {error.message}"
        ) from error
    except Unresolvable as error:
        raise ValueError(
            f"Invalid model schema {MODEL_SCHEMA}: unresolved reference {error.ref}"
        ) from error
    check_version(model["version"], MODEL_SCHEMA, descriptor, LOGGER)

    source = model.get("hfDownload")
    if source is None:
        return None

    model_file = descriptor.parent / model["modelFile"]
    model_dir = descriptor.parent.resolve()
    destination = (model_dir / model["modelFile"]).resolve()
    if not destination.is_relative_to(model_dir):
        raise ValueError(
            f"modelFile resolves outside its model directory: {descriptor}"
        )
    if destination.exists() and not destination.is_file():
        raise ValueError(f"modelFile destination is not a file: {descriptor}")
    try:
        destination.parent.mkdir(parents=True, exist_ok=True)
    except OSError as error:
        raise ValueError(
            f"Cannot prepare modelFile destination for {descriptor}: {error}"
        ) from error
    return model_file, destination, source


def _download_model(model_file, destination, source, token, credential_cache) -> None:
    source_extension = Path(source["filename"]).suffix.lower()
    destination_extension = destination.suffix.lower()
    if source_extension != destination_extension:
        LOGGER.warning(
            "WARNING: %s uses %s, but %s uses %s; saving as configured.",
            source["filename"],
            source_extension or "no extension",
            model_file,
            destination_extension or "no extension",
        )

    LOGGER.info(
        "Downloading %s/%s to %s.",
        source["repo_id"],
        source["filename"],
        model_file,
    )
    expected_sha256 = source.get("sha256")
    try:
        downloaded = hf_hub_download(
            repo_id=source["repo_id"],
            revision=source["revision"],
            filename=source["filename"],
            token=token or False,
            cache_dir=credential_cache,
        )
        _install_download(Path(downloaded), destination, expected_sha256)
    except Exception as error:
        if expected_sha256 is not None and not _matches_sha256(
            destination, expected_sha256
        ):
            destination.unlink(missing_ok=True)
        LOGGER.warning("Skipping %s: %s", model_file, error)


def _install_download(source: Path, destination: Path, expected_sha256: str | None) -> None:
    partial = destination.with_name(destination.name + ".part")
    partial.unlink(missing_ok=True)
    digest = hashlib.sha256()
    try:
        with source.open("rb") as source_file, partial.open("xb") as destination_file:
            while chunk := source_file.read(1024 * 1024):
                destination_file.write(chunk)
                digest.update(chunk)
            destination_file.flush()
            os.fsync(destination_file.fileno())

        actual_sha256 = digest.hexdigest()
        if expected_sha256 is not None and actual_sha256 != expected_sha256:
            raise ValueError(
                f"SHA-256 mismatch: expected {expected_sha256}, got {actual_sha256}"
            )
        partial.chmod(0o644)
        os.replace(partial, destination)
    finally:
        partial.unlink(missing_ok=True)


def _matches_sha256(path: Path, expected_sha256: str) -> bool:
    if not path.is_file():
        return False
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError:
        return False
    return digest.hexdigest() == expected_sha256


def main(models_dir: Path) -> None:
    token = os.environ.get("HF_TOKEN")
    if not token:
        LOGGER.info(
            "No Hugging Face token supplied; downloading public models anonymously."
        )

    validator = _load_validator()
    downloads = []
    for descriptor in sorted(models_dir.rglob("*.json")):
        if download := _prepare_download(descriptor, validator):
            downloads.append(download)

    credential_cache = _cache_dir(token)
    for model_file, destination, source in downloads:
        _download_model(model_file, destination, source, token, credential_cache)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    arguments = parse_args()
    try:
        main(arguments.models_dir)
    except (OSError, ValueError) as error:
        LOGGER.error("%s", error)
        raise SystemExit(1) from None
