#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from hashlib import sha256
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


PREPARE_SCRIPT = Path(__file__).resolve().parents[1] / "prepare-modelfetch-release.sh"
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
        self.manifest = self.root / "release.json"
        self.write_manifest()
        self.write_fake_gh()

    def write_manifest(
        self,
        *,
        sha256_overrides: dict[str, str] | None = None,
        filename_overrides: dict[str, str] | None = None,
    ):
        sha256_overrides = sha256_overrides or {}
        filename_overrides = filename_overrides or {}
        self.manifest.write_text(
            json.dumps(
                {
                    "repository": "Arm-Debug/modelfetch",
                    "tag": self.release_tag,
                    "sourceCommit": "a" * 40,
                    "sdks": {
                        architecture: {
                            "filename": filename_overrides.get(
                                architecture, self.sdk_names[architecture]
                            ),
                            "sha256": sha256_overrides.get(
                                architecture,
                                sha256(
                                    self.sdks[architecture].read_bytes()
                                ).hexdigest(),
                            ),
                        }
                        for architecture in ("amd64", "arm64")
                    },
                }
            ),
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

    def test_rejects_release_for_a_different_source_commit(self):
        document = json.loads(self.manifest.read_text(encoding="utf-8"))
        document["sourceCommit"] = "b" * 40
        self.manifest.write_text(json.dumps(document), encoding="utf-8")

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
            ("sourceCommit", "not-a-commit"),
            ("amd64 filename", ""),
        ):
            with self.subTest(field=field):
                document = json.loads(self.manifest.read_text(encoding="utf-8"))
                if field in {"tag", "sourceCommit"}:
                    document[field] = value
                else:
                    document["sdks"]["amd64"]["filename"] = value
                self.manifest.write_text(json.dumps(document), encoding="utf-8")

                completed = self.run_prepare()

                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("Invalid", completed.stderr)
                self.assertFalse(self.marker.exists())
                self.write_manifest()


if __name__ == "__main__":
    unittest.main()
