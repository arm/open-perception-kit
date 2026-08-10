#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import argparse
import hashlib
import json
import logging
import shutil
from pathlib import Path

from huggingface_hub import hf_hub_download
from huggingface_hub.constants import HF_HUB_CACHE
from jsonschema import Draft202012Validator
from jsonschema.exceptions import SchemaError, ValidationError
from referencing.exceptions import Unresolvable

LOGGER = logging.getLogger(__name__)
MODEL_SCHEMA = (
    Path(__file__).resolve().parents[1] / "config/schemas/v1/model.schema.json"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download model artifacts declared by PEK model descriptors.",
    )
    parser.add_argument(
        "--models-dir",
        required=True,
        type=Path,
        help="Directory recursively searched for JSON model descriptors.",
    )
    parser.add_argument(
        "--token",
        nargs="?",
        help=(
            "Hugging Face token. If the option or its value is omitted, "
            "public models are downloaded anonymously."
        ),
    )
    args = parser.parse_args()
    if not args.models_dir.is_dir():
        parser.error(f"--models-dir is not a directory: {args.models_dir}")
    return args


def _cache_dir(token: str | None) -> Path:
    # Keep each credential from reading artifacts cached by another auth context.
    namespace = hashlib.sha256(token.encode()).hexdigest() if token else "anonymous"
    return Path(HF_HUB_CACHE) / namespace


def main(models_dir: Path, token: str | None) -> None:
    if not token:
        LOGGER.info(
            "No Hugging Face token supplied; downloading public models anonymously."
        )

    credential_cache = _cache_dir(token)
    try:
        schema = json.loads(MODEL_SCHEMA.read_text())
        Draft202012Validator.check_schema(schema)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"Invalid model schema {MODEL_SCHEMA}: {error}") from error
    except SchemaError as error:
        raise ValueError(
            f"Invalid model schema {MODEL_SCHEMA}: {error.message}"
        ) from error
    validator = Draft202012Validator(schema)
    downloads = []

    for descriptor in sorted(models_dir.rglob("*.json")):
        try:
            model = json.loads(descriptor.read_text())
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            raise ValueError(f"Invalid model descriptor {descriptor}: {error}") from error
        if not isinstance(model, dict) or not {"modelFile", "hfDownload"} & model.keys():
            continue

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

        source = model.get("hfDownload")
        if source is None:
            continue

        model_file = descriptor.parent / model["modelFile"]
        model_dir = descriptor.parent.resolve()
        destination = (model_dir / model["modelFile"]).resolve()
        if not destination.is_relative_to(model_dir):
            raise ValueError(
                f"modelFile resolves outside its model directory: {descriptor}"
            )
        try:
            destination.parent.mkdir(parents=True, exist_ok=True)
        except OSError as error:
            raise ValueError(
                f"Cannot prepare modelFile destination for {descriptor}: {error}"
            ) from error

        downloads.append((model_file, destination, source))

    for model_file, destination, source in downloads:
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
        try:
            downloaded = hf_hub_download(
                repo_id=source["repo_id"],
                revision=source["revision"],
                filename=source["filename"],
                token=token or False,
                cache_dir=credential_cache,
            )
            shutil.copyfile(downloaded, destination)
            destination.chmod(0o644)
        except Exception as error:
            LOGGER.warning("Skipping %s: %s", model_file, error)
            continue


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    arguments = parse_args()
    try:
        main(arguments.models_dir, arguments.token)
    except (OSError, ValueError) as error:
        LOGGER.error("%s", error)
        raise SystemExit(1) from None
