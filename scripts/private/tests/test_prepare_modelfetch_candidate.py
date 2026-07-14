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


PREPARE_SCRIPT = Path(__file__).resolve().parents[1] / "prepare-modelfetch-candidate.sh"
REPO_ROOT = Path(__file__).resolve().parents[3]
BLACKDUCK_WORKFLOW = REPO_ROOT / ".github/workflows/blackduck-scan.yml"


class CandidateWorkflowTests(unittest.TestCase):
    def test_black_duck_source_scan_excludes_candidate_cache(self):
        workflow = BLACKDUCK_WORKFLOW.read_text(encoding="utf-8")
        excluded_line = next(
            line
            for line in workflow.splitlines()
            if "--detect.excluded.directories=" in line
        )
        excluded_directories = excluded_line.split('="', 1)[1].split('"', 1)[0]

        self.assertIn(".cache", excluded_directories.split(","))


class PrepareModelfetchCandidateTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.root = Path(self.tempdir.name)
        self.bin_dir = self.root / "bin"
        self.bin_dir.mkdir()
        self.wheel = self.root / "fixture.whl"
        self.wheel.write_bytes(b"verified candidate wheel")
        self.wheel_name = "modelfetch-test-py3-none-any.whl"
        self.artifact_name = "candidate-artifact"
        self.marker = self.root / "gh-called"
        self.cache = self.root / "cache"
        self.manifest = self.root / "candidate.json"
        self.write_manifest(sha256(self.wheel.read_bytes()).hexdigest())
        self.write_fake_gh()

    def write_manifest(
        self, wheel_sha256: str, *, expires_at: str = "2099-01-01T00:00:00Z"
    ):
        self.manifest.write_text(
            json.dumps(
                {
                    "repository": "Arm-Debug/modelfetch",
                    "runId": 123,
                    "artifactName": self.artifact_name,
                    "wheelFilename": self.wheel_name,
                    "wheelSha256": wheel_sha256,
                    "expiresAt": expires_at,
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
cp "$FAKE_WHEEL" "$destination/$FAKE_WHEEL_NAME"
""",
            encoding="utf-8",
        )
        fake_gh.chmod(0o755)

    def run_prepare(
        self, *, auth_fails: bool = False
    ) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        env.update(
            {
                "PATH": f"{self.bin_dir}:{env['PATH']}",
                "MODELFETCH_CANDIDATE_MANIFEST": str(self.manifest),
                "MODELFETCH_CACHE_ROOT": str(self.cache),
                "FAKE_GH_MARKER": str(self.marker),
                "FAKE_WHEEL": str(self.wheel),
                "FAKE_WHEEL_NAME": self.wheel_name,
                "FAKE_GH_AUTH_FAIL": "1" if auth_fails else "0",
            }
        )
        return subprocess.run(
            ["bash", str(PREPARE_SCRIPT)],
            check=False,
            capture_output=True,
            text=True,
            env=env,
        )

    def test_downloads_once_and_reuses_verified_cached_wheel(self):
        first = self.run_prepare()
        second = self.run_prepare()

        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)
        destination = self.cache / "modelfetch-candidate.whl"
        self.assertEqual(Path(first.stdout.strip()), destination)
        self.assertEqual(destination.read_bytes(), self.wheel.read_bytes())
        self.assertEqual(self.marker.read_text(encoding="utf-8"), "called\n")

    def test_rejects_checksum_mismatch_without_populating_cache(self):
        self.write_manifest("0" * 64)

        completed = self.run_prepare()

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("Checksum mismatch", completed.stderr)
        self.assertFalse((self.cache / "modelfetch-candidate.whl").exists())

    def test_replaces_stale_candidate_at_stable_cache_path(self):
        destination = self.cache / "modelfetch-candidate.whl"
        destination.parent.mkdir(parents=True)
        destination.write_bytes(b"stale candidate")

        completed = self.run_prepare()

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(Path(completed.stdout.strip()), destination)
        self.assertEqual(destination.read_bytes(), self.wheel.read_bytes())
        self.assertEqual(self.marker.read_text(encoding="utf-8"), "called\n")

    def test_rejects_expired_candidate_before_download(self):
        self.write_manifest(
            sha256(self.wheel.read_bytes()).hexdigest(),
            expires_at="2020-01-01T00:00:00Z",
        )

        completed = self.run_prepare()

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("expired", completed.stderr)
        self.assertFalse(self.marker.exists())

    def test_requires_authenticated_github_cli_before_download(self):
        completed = self.run_prepare(auth_fails=True)

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("Actions read access", completed.stderr)
        self.assertFalse(self.marker.exists())

    def test_rejects_unsafe_stable_cache_entry_before_download(self):
        destination = self.cache / "modelfetch-candidate.whl"
        destination.mkdir(parents=True)

        completed = self.run_prepare()

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("unsafe modelfetch candidate cache entry", completed.stderr)
        self.assertFalse(self.marker.exists())

    def test_rejects_unsafe_manifest_path_components(self):
        for field, value in (("artifactName", ".."), ("wheelFilename", "")):
            with self.subTest(field=field):
                document = json.loads(self.manifest.read_text(encoding="utf-8"))
                document[field] = value
                self.manifest.write_text(json.dumps(document), encoding="utf-8")

                completed = self.run_prepare()

                self.assertNotEqual(completed.returncode, 0)
                self.assertIn(f"Invalid {field}", completed.stderr)
                self.assertFalse(self.marker.exists())
                self.write_manifest(sha256(self.wheel.read_bytes()).hexdigest())


if __name__ == "__main__":
    unittest.main()
