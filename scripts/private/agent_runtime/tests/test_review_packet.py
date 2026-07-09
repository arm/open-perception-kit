################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "review" / "packet.py"


def import_script():
    spec = importlib.util.spec_from_file_location("agent_review_packet", SCRIPT_PATH)
    assert spec is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def git(repo: Path, *args: str) -> str:
    return subprocess.run(
        ["git", *args],
        cwd=repo,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    ).stdout.rstrip()


class AgentReviewPacketTests(unittest.TestCase):
    def test_build_packet_summarizes_changed_scope_from_git(self) -> None:
        packet = import_script()
        with tempfile.TemporaryDirectory() as tmpdir:
            repo = Path(tmpdir)
            git(repo, "init")
            git(repo, "config", "user.email", "agent@example.com")
            git(repo, "config", "user.name", "Agent")
            (repo / "script.py").write_text("print('old')\n", encoding="utf-8")
            git(repo, "add", "script.py")
            git(repo, "commit", "-m", "base")
            base_sha = git(repo, "rev-parse", "HEAD")
            (repo / "script.py").write_text("print('new')\n", encoding="utf-8")
            (repo / ".github" / "workflows").mkdir(parents=True)
            (repo / ".github" / "workflows" / "ci.yml").write_text("name: CI\n", encoding="utf-8")
            git(repo, "add", ".")
            git(repo, "commit", "-m", "head")
            head_sha = git(repo, "rev-parse", "HEAD")
            context = repo / "review-context.json"
            context.write_text(
                json.dumps({"review_scope": {"base_sha": base_sha, "head_sha": head_sha}}),
                encoding="utf-8",
            )

            packet_dir = repo / "packet"
            index = packet.write_packet(repo, context, packet_dir)

            self.assertEqual(index, packet_dir / "index.md")
            self.assertTrue(index.is_file())
            self.assertTrue((packet_dir / "diff-stat.txt").is_file())
            self.assertTrue((packet_dir / "changed-files.txt").is_file())
            self.assertEqual(
                (packet_dir / "diff-stat.txt").read_text(encoding="utf-8"),
                git(repo, "diff", "--stat", f"{base_sha}...{head_sha}") + "\n",
            )
            self.assertEqual(
                (packet_dir / "changed-files.txt").read_text(encoding="utf-8"),
                git(repo, "diff", "--name-status", f"{base_sha}...{head_sha}") + "\n",
            )
            self.assertEqual(
                (packet_dir / "hunks" / "script.py.diff").read_text(encoding="utf-8"),
                git(repo, "diff", "--no-ext-diff", "--unified=60", f"{base_sha}...{head_sha}", "--", "script.py")
                + "\n",
            )
            self.assertEqual(
                (packet_dir / "hunks" / ".github__workflows__ci.yml.diff").read_text(encoding="utf-8"),
                git(
                    repo,
                    "diff",
                    "--no-ext-diff",
                    "--unified=60",
                    f"{base_sha}...{head_sha}",
                    "--",
                    ".github/workflows/ci.yml",
                ) + "\n",
            )


if __name__ == "__main__":
    unittest.main()
