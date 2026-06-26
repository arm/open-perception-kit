################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from pathlib import Path
import json
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[3]
PATCH_SCRIPT = REPO_ROOT / "scripts/private/apply_workflow_freshness_updates.py"


class WorkflowFreshnessPatchTests(unittest.TestCase):
    def test_patch_script_updates_patchable_workflow_refs(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            workflow_path = repo_root / ".github/workflows/example.yml"
            workflow_path.parent.mkdir(parents=True, exist_ok=True)
            workflow_path.write_text(
                "jobs:\n  test:\n    steps:\n      - uses: actions/checkout@v5\n",
                encoding="utf-8",
            )

            report_path = repo_root / "workflow-dependency-freshness.json"
            report_path.write_text(
                json.dumps(
                    {
                        "entries": [
                            {
                                "repository": "actions/checkout",
                                "current_ref": "v5",
                                "latest_ref": "v6",
                                "status": "behind",
                                "usages": [".github/workflows/example.yml:4"],
                            },
                            {
                                "repository": "actions/upload-artifact",
                                "current_ref": "v1",
                                "latest_ref": "v2",
                                "status": "pinned",
                                "usages": [".github/workflows/example.yml:4"],
                            },
                        ],
                    },
                    indent=2,
                ) + "\n",
                encoding="utf-8",
            )

            subprocess.run(
                [
                    "python3",
                    str(PATCH_SCRIPT),
                    "--repo-root",
                    str(repo_root),
                    "--report-json",
                    str(report_path),
                    "--skip-ref-check",
                ],
                check=True,
            )

            updated = workflow_path.read_text(encoding="utf-8")
            self.assertIn("actions/checkout@v6", updated)
            self.assertNotIn("actions/checkout@v5", updated)


if __name__ == "__main__":
    unittest.main()
