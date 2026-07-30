#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import json
import os
import shutil
from pathlib import Path

from huggingface_hub import HfApi, hf_hub_download
from huggingface_hub.errors import HfHubHTTPError


repo_root = Path(__file__).resolve().parent.parent
token = os.environ.get("HF_TOKEN")

if token:
    try:
        HfApi().whoami(token=token)
    except HfHubHTTPError as error:
        if error.response.status_code not in (401, 403):
            raise
        raise SystemExit(
            "FAILED: the supplied Hugging Face token was rejected."
        ) from None
    print("Connected to Hugging Face with the supplied token.", flush=True)
else:
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
        print(f"Skipping {model_file}: no hfDownload source.", flush=True)
        continue

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
    except HfHubHTTPError as error:
        if error.response.status_code not in (401, 403):
            raise
        reason = (
            "the supplied Hugging Face token does not grant access"
            if token
            else "an authorized Hugging Face token is required"
        )
        print(f"Skipping {model_file}: {reason}.", flush=True)
        continue
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(downloaded, destination)
    destination.chmod(0o644)
