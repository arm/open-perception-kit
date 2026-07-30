#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import json
import shutil
from pathlib import Path

from huggingface_hub import HfApi, hf_hub_download


repo_root = Path(__file__).resolve().parent.parent

try:
    HfApi().whoami()
except Exception:
    print(
        "Hugging Face authentication failed; skipping model downloads.",
        flush=True,
    )
    raise SystemExit(0)

print("Connected to Hugging Face.", flush=True)

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
    downloaded = hf_hub_download(
        repo_id=source["repo_id"],
        revision=source["revision"],
        filename=source["filename"],
    )
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(downloaded, destination)
    destination.chmod(0o644)
