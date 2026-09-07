################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Acquire the pinned external model without ever loading unverified bytes."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import sys
from typing import Any
import urllib.error
import urllib.parse
import urllib.request

from support.runtime import ExampleError, ShutdownState


MODEL_URL = (
    "https://huggingface.co/fernandotonon/QtMeshEditor-blazeface-onnx/resolve/"
    "50f2c66ffbdf84beae8c267df2b49e5c5a5162e9/face_detector.onnx"
)
MODEL_SHA256 = (
    "02a04d5d37c3558dc4d5274f7f8f0f0f01ac94e46c5ffb2cee82395d47e23181"  # pragma: allowlist secret
)
MAX_MODEL_BYTES = 64 * 1024 * 1024
DOWNLOAD_TIMEOUT_SECONDS = 30


class HttpsOnlyRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(
        self,
        request: urllib.request.Request,
        file_pointer: Any,
        code: int,
        message: str,
        headers: Any,
        new_url: str,
    ) -> urllib.request.Request | None:
        if urllib.parse.urlsplit(new_url).scheme.lower() != "https":
            raise urllib.error.HTTPError(
                new_url,
                code,
                "refusing non-HTTPS model redirect",
                headers,
                file_pointer,
            )
        return super().redirect_request(
            request,
            file_pointer,
            code,
            message,
            headers,
            new_url,
        )


def ensure_model(model_path: Path, shutdown: ShutdownState) -> None:
    """Reuse a verified cache or atomically install a verified download."""

    partial_path = model_path.with_name(model_path.name + ".part")
    shutdown.check()
    if model_path.is_file():
        existing_hash = _sha256(model_path, shutdown)
        if existing_hash == MODEL_SHA256:
            print(f"Using verified cached model: {model_path}", file=sys.stderr)
            return
        print(
            f"Cached model has SHA-256 {existing_hash}; downloading a verified replacement.",
            file=sys.stderr,
        )

    partial_path.unlink(missing_ok=True)
    print(f"Downloading pinned BlazeFace model to {partial_path.name}...", file=sys.stderr)
    request = urllib.request.Request(MODEL_URL, headers={"User-Agent": "PEK-BYOM-BlazeFace/1"})
    digest = hashlib.sha256()
    downloaded = 0
    expected_length = None
    try:
        with open_model_url(request) as response:
            final_url = response.geturl()
            if urllib.parse.urlsplit(final_url).scheme.lower() != "https":
                raise ExampleError(f"model download redirected to non-HTTPS URL: {final_url}")
            length_header = response.headers.get("Content-Length")
            if length_header is not None:
                try:
                    expected_length = int(length_header)
                except ValueError as exc:
                    raise ExampleError(f"invalid model Content-Length {length_header!r}") from exc
                if expected_length < 1 or expected_length > MAX_MODEL_BYTES:
                    raise ExampleError(
                        f"model response size {expected_length} exceeds the allowed limit"
                    )

            with partial_path.open("xb") as destination:
                while True:
                    chunk = response.read(1024 * 1024)
                    shutdown.check()
                    if not chunk:
                        break
                    downloaded += len(chunk)
                    if downloaded > MAX_MODEL_BYTES:
                        raise ExampleError(
                            f"model download exceeded the {MAX_MODEL_BYTES}-byte limit"
                        )
                    destination.write(chunk)
                    digest.update(chunk)
                destination.flush()
                os.fsync(destination.fileno())

        if downloaded == 0:
            raise ExampleError("model download was empty")
        if expected_length is not None and downloaded != expected_length:
            raise ExampleError(
                f"incomplete model download: expected {expected_length} bytes, got {downloaded}"
            )
        actual_hash = digest.hexdigest()
        if actual_hash != MODEL_SHA256:
            raise ExampleError(
                f"model SHA-256 mismatch: expected {MODEL_SHA256}, got {actual_hash}"
            )
        shutdown.check()
        os.replace(partial_path, model_path)
        print(f"Downloaded and verified {downloaded} bytes.", file=sys.stderr)
    except (urllib.error.URLError, OSError) as exc:
        raise ExampleError(f"model download failed: {exc}") from exc
    finally:
        partial_path.unlink(missing_ok=True)


def open_model_url(request: urllib.request.Request):
    opener = urllib.request.build_opener(HttpsOnlyRedirectHandler())
    return opener.open(request, timeout=DOWNLOAD_TIMEOUT_SECONDS)


def _sha256(path: Path, shutdown: ShutdownState) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            shutdown.check()
            digest.update(chunk)
    shutdown.check()
    return digest.hexdigest()
