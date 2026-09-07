#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import urllib.parse


COMPILER_CACHE_KEY = re.compile(r"^(pek-ccache-.+-)([0-9a-f]{40})$")
SHA = re.compile(r"[0-9a-f]{40}")


def github_repository() -> str:
    value = os.environ.get("GITHUB_REPOSITORY", "").strip().lower()
    if value.count("/") != 1:
        raise RuntimeError("GITHUB_REPOSITORY must contain owner/repository.")
    return value


def run(command: list[str], *, capture_output: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        check=True,
        text=True,
        stdout=subprocess.PIPE if capture_output else None,
        stderr=subprocess.PIPE if capture_output else None,
    )


def read_json(command: list[str]) -> object:
    return json.loads(run(command, capture_output=True).stdout)


def list_caches(ref: str) -> list[dict[str, object]]:
    repository = github_repository()
    result = run(
        [
            "gh",
            "api",
            "--paginate",
            "--method",
            "GET",
            f"repos/{repository}/actions/caches",
            "-f",
            f"ref={ref}",
            "-f",
            "per_page=100",
            "--jq",
            ".actions_caches[] | @json",
        ],
        capture_output=True,
    )
    caches = [json.loads(line) for line in result.stdout.splitlines()]
    if any(
        not isinstance(cache, dict)
        or not isinstance(cache.get("id"), int)
        or not isinstance(cache.get("key"), str)
        for cache in caches
    ):
        raise ValueError("GitHub Actions cache response contains an invalid cache.")
    return caches


def superseded_compiler_caches(
    caches: list[dict[str, object]], current_head_sha: str
) -> list[dict[str, object]]:
    if not SHA.fullmatch(current_head_sha):
        raise ValueError("Current head must be a 40-character lowercase commit SHA.")
    parsed = [
        (cache, match.group(1), match.group(2))
        for cache in caches
        if isinstance(cache.get("key"), str)
        and (match := COMPILER_CACHE_KEY.fullmatch(str(cache["key"])))
    ]
    replaced_prefixes = {
        prefix for _cache, prefix, cache_sha in parsed if cache_sha == current_head_sha
    }
    return [
        cache
        for cache, prefix, cache_sha in parsed
        if cache_sha != current_head_sha and prefix in replaced_prefixes
    ]


def delete_caches(caches: list[dict[str, object]], reason: str) -> None:
    repository = github_repository()
    for cache in caches:
        print(f"Deleting {reason} cache: {cache['key']}")
        run(
            [
                "gh",
                "api",
                "--method",
                "DELETE",
                f"repos/{repository}/actions/caches/{cache['id']}",
            ]
        )


def reconcile_pull_request(number: str) -> None:
    if not number.isdigit() or int(number) <= 0:
        raise ValueError("Pull request number must be a positive integer.")
    repository = github_repository()
    payload = read_json(
        ["gh", "api", "--method", "GET", f"repos/{repository}/pulls/{number}"]
    )
    if not isinstance(payload, dict):
        raise ValueError("GitHub pull request response must be an object.")
    state = payload.get("state")
    head = payload.get("head")
    head_sha = head.get("sha") if isinstance(head, dict) else None
    cache_ref = f"refs/pull/{number}/merge"
    caches = list_caches(cache_ref)
    if state == "closed":
        print(f"PR is closed; deleting all caches for {cache_ref}")
        delete_caches(caches, "closed PR")
        return
    if state != "open" or not isinstance(head_sha, str):
        raise ValueError("GitHub pull request response has an invalid state or head.")
    delete_caches(superseded_compiler_caches(caches, head_sha), "superseded")


def reconcile_branch(branch: str) -> None:
    if not branch:
        raise ValueError("Branch name is required for a branch event.")
    repository = github_repository()
    encoded_branch = urllib.parse.quote(branch, safe="")
    payload = read_json(
        [
            "gh",
            "api",
            "--method",
            "GET",
            f"repos/{repository}/branches/{encoded_branch}",
        ]
    )
    if not isinstance(payload, dict):
        raise ValueError("GitHub branch response must be an object.")
    commit = payload.get("commit")
    head_sha = commit.get("sha") if isinstance(commit, dict) else None
    if not isinstance(head_sha, str):
        raise ValueError("GitHub branch response has an invalid head.")
    caches = list_caches(f"refs/heads/{branch}")
    delete_caches(superseded_compiler_caches(caches, head_sha), "superseded")


def delete_ref(ref: str) -> None:
    delete_caches(list_caches(ref), "retired ref")


def delete_key(ref: str, key: str) -> None:
    delete_caches(
        [cache for cache in list_caches(ref) if cache["key"] == key],
        "retired key",
    )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Clean up GitHub Actions caches.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    reconcile_parser = subparsers.add_parser("reconcile")
    reconcile_parser.add_argument(
        "--event", required=True, choices=("pull_request", "push", "schedule", "workflow_dispatch")
    )
    reconcile_parser.add_argument("--pr-number", default="")
    reconcile_parser.add_argument("--branch", default="")

    delete_ref_parser = subparsers.add_parser("delete-ref")
    delete_ref_parser.add_argument("--ref", required=True)

    delete_key_parser = subparsers.add_parser("delete-key")
    delete_key_parser.add_argument("--ref", required=True)
    delete_key_parser.add_argument("--key", required=True)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.command == "reconcile":
        if args.event == "pull_request":
            reconcile_pull_request(args.pr_number)
        else:
            if args.pr_number:
                raise ValueError("A branch event cannot include a pull request number.")
            reconcile_branch(args.branch)
    elif args.command == "delete-ref":
        delete_ref(args.ref)
    else:
        delete_key(args.ref, args.key)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Actions cache cleanup error: {error}", file=sys.stderr)
        raise SystemExit(1) from error
