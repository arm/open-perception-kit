#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import json
import os
import re
import subprocess
import sys
from urllib.parse import quote


CACHE_KEY = re.compile(r"^(opk-ccache-.+-)([0-9a-f]{40})$")


def gh(*args: str) -> str:
    return subprocess.run(
        ["gh", "api", *args], check=True, text=True, stdout=subprocess.PIPE
    ).stdout


def endpoint(path: str) -> str:
    return f"repos/{os.environ['GITHUB_REPOSITORY']}/{path}"


def get(path: str) -> dict:
    return json.loads(gh("--method", "GET", endpoint(path)))


def list_caches(ref: str) -> list[dict]:
    output = gh(
        "--paginate",
        "--method",
        "GET",
        endpoint("actions/caches"),
        "-f",
        f"ref={ref}",
        "-f",
        "per_page=100",
        "--jq",
        ".actions_caches[] | @json",
    )
    return [json.loads(line) for line in output.splitlines()]


def superseded(caches: list[dict], head: str) -> list[dict]:
    parsed = []
    for cache in caches:
        if match := CACHE_KEY.fullmatch(cache["key"]):
            parsed.append((cache, *match.groups()))
    replaced = {prefix for _cache, prefix, sha in parsed if sha == head}
    return [
        cache
        for cache, prefix, sha in parsed
        if sha != head and prefix in replaced
    ]


def delete(caches: list[dict]) -> None:
    for cache in caches:
        print(f"Deleting cache: {cache['key']}")
        gh("--method", "DELETE", endpoint(f"actions/caches/{cache['id']}"))


def reconcile(event: str, number: str, branch: str) -> None:
    if event == "pull_request":
        if not number.isdigit() or int(number) < 1:
            raise ValueError("invalid pull request number")
        pull = get(f"pulls/{number}")
        ref = f"refs/pull/{number}/merge"
        caches = list_caches(ref)
        if pull["state"] == "closed":
            delete(caches)
            return
        if pull["state"] != "open":
            raise ValueError("invalid pull request state")
        head = pull["head"]["sha"]
    elif event in {"push", "schedule", "workflow_dispatch"} and not number and branch:
        head = get(f"branches/{quote(branch, safe='')}")["commit"]["sha"]
        caches = list_caches(f"refs/heads/{branch}")
    else:
        raise ValueError("invalid branch event context")
    delete(superseded(caches, head))


def main(args: list[str]) -> None:
    command, *values = args
    if command == "reconcile" and len(values) == 3:
        reconcile(*values)
    elif command == "delete-ref" and len(values) == 1:
        delete(list_caches(values[0]))
    elif command == "delete-key" and len(values) == 2:
        ref, key = values
        delete([cache for cache in list_caches(ref) if cache["key"] == key])
    else:
        raise ValueError("invalid command")


if __name__ == "__main__":
    main(sys.argv[1:])
