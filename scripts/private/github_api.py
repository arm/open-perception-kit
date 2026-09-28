#!/usr/bin/env python3
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

from __future__ import annotations

from collections.abc import Mapping
import http.client
import io
import json
import os
from pathlib import Path
import re
import shutil
import stat
import urllib.error
import urllib.parse
import urllib.request
import zipfile

GITHUB_API_VERSION = "2022-11-28"
GITHUB_USER_AGENT = "open-perception-kit-github-api"

GITHUB_ARCHIVE_REDIRECT_HOST_SUFFIXES = (
    ".actions.githubusercontent.com",
    ".blob.core.windows.net",
    ".githubusercontent.com",
)
GITHUB_ARCHIVE_DOWNLOAD_TIMEOUT_SECONDS = 60


def github_api_base_url() -> str:
    return os.environ.get("GITHUB_API_URL", "https://api.github.com").rstrip("/")


def url_hostname(url: str) -> str:
    hostname = urllib.parse.urlsplit(url).hostname
    if not hostname:
        raise ValueError(f"URL is missing a hostname: {url}")
    return hostname.lower()


def github_api_hostname() -> str:
    return url_hostname(github_api_base_url())


def github_api_endpoint_url(endpoint: str) -> str:
    parsed = urllib.parse.urlsplit(endpoint)
    if parsed.scheme or parsed.netloc:
        raise ValueError("GitHub API endpoint must be relative.")
    return f"{github_api_base_url()}/{endpoint.lstrip('/')}"


def github_api_query_endpoint(endpoint: str, params: Mapping[str, object]) -> str:
    query = urllib.parse.urlencode(
        {key: str(value) for key, value in params.items() if value is not None}
    )
    clean_endpoint = endpoint.lstrip("/")
    return f"{clean_endpoint}?{query}" if query else clean_endpoint


def github_api_token() -> str:
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN", "")
    if not token:
        raise RuntimeError("GITHUB_TOKEN or GH_TOKEN is required for GitHub API access.")
    return token


def github_api_headers(
    *,
    token: str | None = None,
    require_token: bool = True,
    accept: str = "application/vnd.github+json",
) -> dict[str, str]:
    headers = {
        "Accept": accept,
        "User-Agent": GITHUB_USER_AGENT,
        "X-GitHub-Api-Version": GITHUB_API_VERSION,
    }
    resolved_token = token
    if require_token and not resolved_token:
        resolved_token = github_api_token()
    if resolved_token:
        headers["Authorization"] = f"Bearer {resolved_token}"
    return headers


def github_api_request(
    endpoint_or_url: str,
    *,
    token: str | None = None,
    require_token: bool = True,
    method: str = "GET",
    payload: object | None = None,
    accept: str = "application/vnd.github+json",
    timeout: float | None = None,
) -> bytes:
    url = endpoint_or_url if urllib.parse.urlsplit(endpoint_or_url).scheme else github_api_endpoint_url(endpoint_or_url)
    data = None
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=data,
        method=method,
        headers=github_api_headers(
            token=token,
            require_token=require_token,
            accept=accept,
        ),
    )
    if timeout is None:
        response_context = urllib.request.urlopen(request)
    else:
        response_context = urllib.request.urlopen(request, timeout=timeout)
    with response_context as response:
        return response.read()


def github_api_json(endpoint: str, *, token: str | None = None) -> object:
    return json.loads(github_api_request(github_api_endpoint_url(endpoint), token=token))


def github_api_json_or_empty(
    endpoint: str,
    *,
    token: str | None = None,
    timeout: float = 20,
) -> object:
    try:
        return json.loads(
            github_api_request(
                endpoint,
                token=token,
                require_token=False,
                timeout=timeout,
            ).decode("utf-8", errors="replace")
        )
    except urllib.error.HTTPError as error:
        error.close()
        return {}
    except (
        OSError,
        urllib.error.URLError,
        TimeoutError,
        UnicodeDecodeError,
        json.JSONDecodeError,
    ):
        return {}


def github_archive_api_url(url: str) -> str:
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https":
        raise ValueError("GitHub archive API URL must use https.")
    if url_hostname(url) != github_api_hostname():
        raise ValueError("GitHub archive API URL must use the configured GitHub API host.")
    base_path = urllib.parse.urlsplit(github_api_base_url()).path.rstrip("/")
    path = parsed.path
    if base_path:
        if not path.startswith(f"{base_path}/"):
            raise ValueError("GitHub archive API URL must use the configured GitHub API path.")
        path = path[len(base_path):]
    if not (
        re.fullmatch(r"/repos/[^/]+/[^/]+/actions/artifacts/\d+/zip", path)
        or re.fullmatch(r"/repos/[^/]+/[^/]+/actions/runs/\d+/logs", path)
    ):
        raise ValueError(f"Unsupported GitHub archive API path: {path}")
    return url


def github_archive_redirect_parts(url: str) -> tuple[str, str]:
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https":
        raise ValueError("GitHub archive redirect URL must use https.")
    hostname = url_hostname(url)
    if not any(hostname.endswith(suffix) for suffix in GITHUB_ARCHIVE_REDIRECT_HOST_SUFFIXES):
        raise ValueError("GitHub archive redirect URL must use a GitHub artifact host.")
    path = urllib.parse.urlunsplit(("", "", parsed.path or "/", parsed.query, ""))
    return hostname, path


def github_archive_redirect_bytes(url: str) -> bytes:
    hostname, path = github_archive_redirect_parts(url)
    connection = http.client.HTTPSConnection(
        hostname,
        timeout=GITHUB_ARCHIVE_DOWNLOAD_TIMEOUT_SECONDS,
    )
    try:
        connection.request(
            "GET",
            path,
            headers={"User-Agent": GITHUB_USER_AGENT},
        )
        response = connection.getresponse()
        if response.status != 200:
            raise RuntimeError(
                f"GitHub archive redirect download failed with HTTP {response.status} {response.reason}"
            )
        return response.read()
    finally:
        connection.close()


class _NoRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, _req, _fp, _code, _msg, _headers, _newurl):  # noqa: ANN001
        return None


def download_github_archive(url: str) -> bytes:
    archive_url = github_archive_api_url(url)
    request = urllib.request.Request(
        archive_url,
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {github_api_token()}",
            "User-Agent": GITHUB_USER_AGENT,
            "X-GitHub-Api-Version": GITHUB_API_VERSION,
        },
    )
    opener = urllib.request.build_opener(_NoRedirectHandler)
    try:
        with opener.open(request) as response:
            return response.read()
    except urllib.error.HTTPError as exc:
        if exc.code not in {301, 302, 303, 307, 308}:
            raise
        location = exc.headers.get("Location", "")
        if not location:
            raise
        return github_archive_redirect_bytes(location)


def archive_member_destination(*, destination: Path, member_name: str) -> Path:
    if not member_name:
        raise RuntimeError("Archive member has an empty path.")
    member_path = Path(member_name)
    if member_path.is_absolute():
        raise RuntimeError(f"Archive member escapes destination: {member_name}")
    target = (destination / member_path).resolve()
    if target != destination and destination not in target.parents:
        raise RuntimeError(f"Archive member escapes destination: {member_name}")
    return target


def archive_member_is_symlink(member: zipfile.ZipInfo) -> bool:
    return stat.S_ISLNK(member.external_attr >> 16)


def extract_archive_bytes(archive_bytes: bytes, destination: Path) -> list[Path]:
    destination.mkdir(parents=True, exist_ok=True)
    destination_root = destination.resolve()
    with zipfile.ZipFile(io.BytesIO(archive_bytes)) as archive:
        members: list[tuple[zipfile.ZipInfo, Path]] = []
        for member in archive.infolist():
            if archive_member_is_symlink(member):
                raise RuntimeError(f"Archive member is a symlink: {member.filename}")
            target = archive_member_destination(
                destination=destination_root,
                member_name=member.filename,
            )
            if target == destination_root and not member.is_dir():
                raise RuntimeError(f"Archive member targets destination root: {member.filename}")
            members.append((member, target))
        for member, target in members:
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(member) as source_file:
                with target.open("wb") as output_file:
                    shutil.copyfileobj(source_file, output_file)
    return sorted(path for path in destination.rglob("*") if path.is_file())
