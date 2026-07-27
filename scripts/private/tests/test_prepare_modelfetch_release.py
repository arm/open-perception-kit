#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from hashlib import sha256
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


PREPARE_SCRIPT = Path(__file__).resolve().parents[1] / "prepare-modelfetch-release.sh"
CONTAINER_PREPARE_SCRIPT = (
    Path(__file__).resolve().parents[1]
    / "prepare-modelfetch-release-in-container.sh"
)
RELEASE_TOOLS_DOCKERFILE = (
    Path(__file__).resolve().parents[1]
    / "modelfetch-release-tools.Dockerfile"
)
REPO_ROOT = Path(__file__).resolve().parents[3]
BLACKDUCK_WORKFLOW = REPO_ROOT / ".github/workflows/blackduck-scan.yml"


class ReleaseWorkflowTests(unittest.TestCase):
    def test_black_duck_source_scan_excludes_release_cache(self):
        workflow = BLACKDUCK_WORKFLOW.read_text(encoding="utf-8")
        excluded_line = next(
            line
            for line in workflow.splitlines()
            if "--detect.excluded.directories=" in line
        )
        excluded_directories = excluded_line.split('="', 1)[1].split('"', 1)[0]

        self.assertIn(".cache", excluded_directories.split(","))


class PrepareModelfetchReleaseInContainerTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.root = Path(self.tempdir.name)
        self.repo = self.root / "repo"
        self.script_dir = self.repo / "scripts/private"
        self.script_dir.mkdir(parents=True)
        self.script = self.script_dir / CONTAINER_PREPARE_SCRIPT.name
        shutil.copy2(CONTAINER_PREPARE_SCRIPT, self.script)
        shutil.copy2(RELEASE_TOOLS_DOCKERFILE, self.script_dir)
        shutil.copy2(PREPARE_SCRIPT, self.script_dir)

        self.bin_dir = self.root / "bin"
        self.bin_dir.mkdir()
        self.docker_log = self.root / "docker.log"
        fake_docker = self.bin_dir / "docker"
        fake_docker.write_text(
            """#!/usr/bin/env bash
set -euo pipefail
printf '%s\\n' "$*" >> "$FAKE_DOCKER_LOG"
if [[ "${1:-}" == "run" && "${FAKE_DOCKER_RUN_FAIL:-0}" == "1" ]]; then
    exit 42
fi
""",
            encoding="utf-8",
        )
        fake_docker.chmod(0o755)

    def run_prepare(
        self,
        *,
        include_token: bool = True,
        include_image: bool = True,
        docker_run_fails: bool = False,
        arguments: tuple[str, ...] = (),
    ) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        env["PATH"] = f"{self.bin_dir}:{env['PATH']}"
        env["FAKE_DOCKER_LOG"] = str(self.docker_log)
        env["FAKE_DOCKER_RUN_FAIL"] = "1" if docker_run_fails else "0"
        env.pop("GH_TOKEN", None)
        env.pop("MODELFETCH_RELEASE_TOOL_IMAGE", None)
        if include_token:
            env["GH_TOKEN"] = "test-token"
        if include_image:
            env["MODELFETCH_RELEASE_TOOL_IMAGE"] = "test-release-tools:123"
        return subprocess.run(
            ["bash", str(self.script), *arguments],
            cwd=self.root,
            check=False,
            capture_output=True,
            text=True,
            env=env,
        )

    def docker_calls(self) -> list[str]:
        if not self.docker_log.exists():
            return []
        return self.docker_log.read_text(encoding="utf-8").splitlines()

    def test_builds_runs_and_removes_the_isolated_release_tool_image(self):
        completed = self.run_prepare()

        self.assertEqual(completed.returncode, 0, completed.stderr)
        calls = self.docker_calls()
        self.assertEqual(len(calls), 3)
        self.assertIn(
            f"build --file {self.script_dir / RELEASE_TOOLS_DOCKERFILE.name}",
            calls[0],
        )
        self.assertIn("--tag test-release-tools:123", calls[0])
        self.assertTrue(calls[0].endswith(str(self.repo)))
        self.assertIn(f"--user {os.getuid()}:{os.getgid()}", calls[1])
        self.assertIn(
            f"--mount type=bind,source={self.repo},target=/workspace,readonly",
            calls[1],
        )
        self.assertIn(
            "--mount "
            f"type=bind,source={self.repo / '.cache/modelfetch'},"
            "target=/modelfetch-cache",
            calls[1],
        )
        self.assertIn("--env GH_TOKEN", calls[1])
        self.assertIn("--env GH_CONFIG_DIR=/tmp/gh-config", calls[1])
        self.assertIn("--env HOME=/tmp", calls[1])
        self.assertIn("--env MODELFETCH_CACHE_ROOT=/modelfetch-cache", calls[1])
        self.assertNotIn("test-token", calls[1])
        self.assertEqual(
            calls[2],
            "image rm --force test-release-tools:123",
        )

    def test_rejects_symlinked_cache_parent_before_running_docker(self):
        real_cache = self.root / "real-cache"
        real_cache.mkdir()
        (self.repo / ".cache").symlink_to(real_cache, target_is_directory=True)

        completed = self.run_prepare()

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("unsafe modelfetch release cache path", completed.stderr)
        self.assertEqual(self.docker_calls(), [])

    def test_rejects_symlinked_cache_root_before_running_docker(self):
        cache_parent = self.repo / ".cache"
        cache_parent.mkdir()
        real_cache = self.root / "real-modelfetch-cache"
        real_cache.mkdir()
        (cache_parent / "modelfetch").symlink_to(
            real_cache,
            target_is_directory=True,
        )

        completed = self.run_prepare()

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("unsafe modelfetch release cache path", completed.stderr)
        self.assertEqual(self.docker_calls(), [])

    def test_removes_the_tool_image_when_the_container_run_fails(self):
        completed = self.run_prepare(docker_run_fails=True)

        self.assertNotEqual(completed.returncode, 0)
        self.assertEqual(
            self.docker_calls()[-1],
            "image rm --force test-release-tools:123",
        )

    def test_requires_the_token_and_unique_tool_image_name(self):
        for missing in ("token", "image"):
            with self.subTest(missing=missing):
                self.docker_log.unlink(missing_ok=True)
                completed = self.run_prepare(
                    include_token=missing != "token",
                    include_image=missing != "image",
                )

                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("is required", completed.stderr)
                self.assertEqual(self.docker_calls(), [])

    def test_rejects_unused_arguments(self):
        completed = self.run_prepare(arguments=("--legacy-mode",))

        self.assertEqual(completed.returncode, 2)
        self.assertIn("does not accept arguments", completed.stderr)
        self.assertEqual(self.docker_calls(), [])


class PrepareModelfetchReleaseTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.root = Path(self.tempdir.name)
        self.bin_dir = self.root / "bin"
        self.bin_dir.mkdir()
        self.sdks_dir = self.root / "sdks"
        self.sdks_dir.mkdir()
        self.sdk_names = {
            "amd64": "modelfetch-c-test-x86_64-unknown-linux-gnu.tar.gz",
            "arm64": "modelfetch-c-test-aarch64-unknown-linux-gnu.tar.gz",
        }
        self.sdks = {
            architecture: self.sdks_dir / filename
            for architecture, filename in self.sdk_names.items()
        }
        self.sdks["amd64"].write_bytes(b"verified amd64 C SDK")
        self.sdks["arm64"].write_bytes(b"verified arm64 C SDK")
        self.release_tag = "v1.2.3"
        self.marker = self.root / "gh-called"
        self.cache = self.root / "cache"
        self.manifest = self.root / "release.manifest"
        self.write_manifest()
        self.write_fake_gh()

    def write_manifest(
        self,
        *,
        sha256_overrides: dict[str, str] | None = None,
        filename_overrides: dict[str, str] | None = None,
        tag: str | None = None,
        source_commit: str | None = None,
    ):
        sha256_overrides = sha256_overrides or {}
        filename_overrides = filename_overrides or {}
        values = {
            "repository": "Arm-Debug/modelfetch",
            "tag": tag if tag is not None else self.release_tag,
            "source_commit": (
                source_commit if source_commit is not None else "a" * 40
            ),
        }
        for architecture in ("amd64", "arm64"):
            values[f"{architecture}_filename"] = filename_overrides.get(
                architecture, self.sdk_names[architecture]
            )
            values[f"{architecture}_sha256"] = sha256_overrides.get(
                architecture,
                sha256(self.sdks[architecture].read_bytes()).hexdigest(),
            )
        self.manifest.write_text(
            "".join(f"{key}={value}\n" for key, value in values.items()),
            encoding="utf-8",
        )

    def write_fake_gh(self):
        fake_gh = self.bin_dir / "gh"
        fake_gh.write_text(
            """#!/usr/bin/env bash
set -euo pipefail
if [[ "${1:-}" == "auth" && "${2:-}" == "status" ]]; then
    [[ "${FAKE_GH_AUTH_FAIL:-0}" != "1" ]]
    exit
fi
if [[ "${1:-}" == "release" && "${2:-}" == "view" ]]; then
    [[ "${FAKE_RELEASE_LOOKUP_FAIL:-0}" != "1" ]] || exit 1
    printf '%s\\t%s\\t%s\\n' "$FAKE_RELEASE_TAG" "$FAKE_RELEASE_DRAFT" "$FAKE_RELEASE_PRERELEASE"
    exit
fi
if [[ "${1:-}" == "api" ]]; then
    printf '%s\\n' "$FAKE_SOURCE_COMMIT"
    exit
fi
printf 'called\\n' >> "$FAKE_GH_MARKER"
destination=""
while [[ $# -gt 0 ]]; do
    if [[ "$1" == "--dir" ]]; then
        destination="$2"
        break
    fi
    shift
done
test -n "$destination"
cp "$FAKE_SDKS_DIR"/*.tar.gz "$destination/"
""",
            encoding="utf-8",
        )
        fake_gh.chmod(0o755)

    def run_prepare(
        self,
        *,
        auth_fails: bool = False,
        release_lookup_fails: bool = False,
        release_draft: bool = False,
        release_prerelease: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        env.update(
            {
                "PATH": f"{self.bin_dir}:{env['PATH']}",
                "MODELFETCH_RELEASE_MANIFEST": str(self.manifest),
                "MODELFETCH_CACHE_ROOT": str(self.cache),
                "FAKE_GH_MARKER": str(self.marker),
                "FAKE_GH_AUTH_FAIL": "1" if auth_fails else "0",
                "FAKE_RELEASE_LOOKUP_FAIL": (
                    "1" if release_lookup_fails else "0"
                ),
                "FAKE_RELEASE_DRAFT": "true" if release_draft else "false",
                "FAKE_RELEASE_PRERELEASE": (
                    "true" if release_prerelease else "false"
                ),
                "FAKE_RELEASE_TAG": self.release_tag,
                "FAKE_SOURCE_COMMIT": "a" * 40,
                "FAKE_SDKS_DIR": str(self.sdks_dir),
            }
        )
        return subprocess.run(
            ["bash", str(PREPARE_SCRIPT)],
            check=False,
            capture_output=True,
            text=True,
            env=env,
        )

    def destinations(self) -> dict[str, Path]:
        cache = self.cache.resolve()
        return {
            architecture: cache / f"modelfetch-release-linux-{architecture}.tar.gz"
            for architecture in ("amd64", "arm64")
        }

    def test_downloads_both_platform_sdks_once_and_reuses_verified_cache(self):
        first = self.run_prepare()
        second = self.run_prepare()

        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)
        destinations = self.destinations()
        self.assertEqual(
            first.stdout.splitlines(),
            [str(destinations["amd64"]), str(destinations["arm64"])],
        )
        for architecture, destination in destinations.items():
            self.assertEqual(
                destination.read_bytes(), self.sdks[architecture].read_bytes()
            )
        self.assertEqual(self.marker.read_text(encoding="utf-8"), "called\n")

    def test_rejects_checksum_mismatch_without_populating_cache(self):
        self.write_manifest(sha256_overrides={"arm64": "0" * 64})

        completed = self.run_prepare()

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("Checksum mismatch", completed.stderr)
        self.assertTrue(
            all(
                not destination.exists() for destination in self.destinations().values()
            )
        )

    def test_replaces_stale_release_and_refreshes_both_platforms(self):
        destination = self.destinations()["amd64"]
        destination.parent.mkdir(parents=True)
        destination.write_bytes(b"stale release")

        completed = self.run_prepare()

        self.assertEqual(completed.returncode, 0, completed.stderr)
        for architecture, cached_sdk in self.destinations().items():
            self.assertEqual(
                cached_sdk.read_bytes(), self.sdks[architecture].read_bytes()
            )
        self.assertEqual(self.marker.read_text(encoding="utf-8"), "called\n")

    def test_requires_authenticated_github_cli_before_download(self):
        completed = self.run_prepare(auth_fails=True)

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("release read access", completed.stderr)
        self.assertFalse(self.marker.exists())

    def test_reports_release_access_when_the_token_cannot_read_the_release(self):
        completed = self.run_prepare(release_lookup_fails=True)

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("release read access", completed.stderr)
        self.assertNotIn("missing, draft, or prerelease", completed.stderr)
        self.assertFalse(self.marker.exists())

    def test_rejects_release_for_a_different_source_commit(self):
        self.write_manifest(source_commit="b" * 40)

        completed = self.run_prepare()

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("expected", completed.stderr)
        self.assertFalse(self.marker.exists())

    def test_rejects_non_stable_release(self):
        for release_kind in ("draft", "prerelease"):
            with self.subTest(release_kind=release_kind):
                completed = self.run_prepare(
                    release_draft=release_kind == "draft",
                    release_prerelease=release_kind == "prerelease",
                )

                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("missing, draft, or prerelease", completed.stderr)
                self.assertFalse(self.marker.exists())

    def test_rejects_unsafe_stable_cache_entry_before_download(self):
        destination = self.destinations()["amd64"]
        destination.mkdir(parents=True)

        completed = self.run_prepare()

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("unsafe modelfetch release cache entry", completed.stderr)
        self.assertFalse(self.marker.exists())

    def test_rejects_symlinked_cache_root_even_when_cache_is_current(self):
        first = self.run_prepare()
        self.assertEqual(first.returncode, 0, first.stderr)
        real_cache = self.root / "real-cache"
        self.cache.rename(real_cache)
        self.cache.symlink_to(real_cache, target_is_directory=True)
        self.marker.unlink()

        completed = self.run_prepare()

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("unsafe modelfetch release cache root", completed.stderr)
        self.assertFalse(self.marker.exists())

    def test_canonicalizes_cache_root_below_symlinked_parent(self):
        real_parent = self.root / "real-parent"
        real_parent.mkdir()
        linked_parent = self.root / "linked-parent"
        linked_parent.symlink_to(real_parent, target_is_directory=True)
        self.cache = linked_parent / "cache"

        completed = self.run_prepare()

        self.assertEqual(completed.returncode, 0, completed.stderr)
        for architecture, sdk in self.sdks.items():
            cached_sdk = (
                real_parent
                / "cache"
                / (f"modelfetch-release-linux-{architecture}.tar.gz")
            )
            self.assertEqual(cached_sdk.read_bytes(), sdk.read_bytes())

    def test_rejects_unsafe_manifest_path_components(self):
        for field, value in (
            ("tag", "not-a-release"),
            ("source_commit", "not-a-commit"),
            ("amd64 filename", ""),
        ):
            with self.subTest(field=field):
                if field == "tag":
                    self.write_manifest(tag=value)
                elif field == "source_commit":
                    self.write_manifest(source_commit=value)
                else:
                    self.write_manifest(filename_overrides={"amd64": value})

                completed = self.run_prepare()

                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("Invalid", completed.stderr)
                self.assertFalse(self.marker.exists())
                self.write_manifest()

    def test_rejects_unknown_duplicate_and_missing_manifest_keys(self):
        valid_manifest = self.manifest.read_text(encoding="utf-8")
        cases = {
            "unknown": valid_manifest + "unexpected=value\n",
            "duplicate": valid_manifest + "tag=v9.9.9\n",
            "missing": "\n".join(
                line
                for line in valid_manifest.splitlines()
                if not line.startswith("arm64_sha256=")
            )
            + "\n",
        }
        for name, contents in cases.items():
            with self.subTest(name=name):
                self.manifest.write_text(contents, encoding="utf-8")

                completed = self.run_prepare()

                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("Invalid modelfetch release manifest", completed.stderr)
                self.assertFalse(self.marker.exists())
                self.write_manifest()

    def test_manifest_is_parsed_as_data_without_executing_shell_syntax(self):
        command_marker = self.root / "manifest-command-ran"
        self.write_manifest(
            filename_overrides={
                "amd64": f"$(touch {command_marker})",
            }
        )

        completed = self.run_prepare()

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("Invalid modelfetch release manifest", completed.stderr)
        self.assertFalse(command_marker.exists())
        self.assertFalse(self.marker.exists())


if __name__ == "__main__":
    unittest.main()
