#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import json
import os
import shutil
from pathlib import Path

from huggingface_hub import hf_hub_download

repo_root = Path(__file__).resolve().parent.parent
token = os.environ.get("HF_TOKEN")

if not token:
    print(
        "No Hugging Face token supplied; downloading public models anonymously.",
        flush=True,
    )

for descriptor in sorted((repo_root / "config" / "models").rglob("*.json")):
    model = json.loads(descriptor.read_text())
    if "modelFile" not in model:
        continue

    model_file = descriptor.parent.relative_to(repo_root) / model["modelFile"]
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
        print(
            f"WARNING: {source['filename']} uses {source_extension or 'no extension'}, "
            f"but {model_file} uses {destination_extension or 'no extension'}; "
            "saving as configured.",
            flush=True,
        )

    print(
        f"Downloading {source['repo_id']}/{source['filename']} to {model_file}.",
        flush=True,
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
        print(f"Skipping {model_file}: {error}", flush=True)
        continue
