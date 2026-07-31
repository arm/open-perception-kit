#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import argparse
import json
import logging
import shutil
from pathlib import Path

from huggingface_hub import hf_hub_download

LOGGER = logging.getLogger(__name__)


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


def main(models_dir: Path, token: str | None) -> None:

    if not token:
        LOGGER.info(
            "No Hugging Face token supplied; downloading public models anonymously."
        )

    for descriptor in sorted(models_dir.rglob("*.json")):
        model = json.loads(descriptor.read_text())
        if "modelFile" not in model:
            continue

        model_file = descriptor.parent / model["modelFile"]
        model_dir = descriptor.parent.resolve()
        destination = (model_dir / model["modelFile"]).resolve()
        if not destination.is_relative_to(model_dir):
            raise ValueError(f"modelFile escapes its model directory: {descriptor}")

        source = model.get("hfDownload")
        if source is None:
            continue

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
            )
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(downloaded, destination)
            destination.chmod(0o644)
        except Exception as error:
            LOGGER.warning("Skipping %s: %s", model_file, error)
            continue


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    arguments = parse_args()
    main(arguments.models_dir, arguments.token)
