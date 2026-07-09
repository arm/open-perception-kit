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
            (repo / "dirty.py").write_text("VALUE = 'clean'\n", encoding="utf-8")
            git(repo, "add", "script.py", "dirty.py")
            git(repo, "commit", "-m", "base")
            base_sha = git(repo, "rev-parse", "HEAD")
            (repo / "script.py").write_text("print('new')\n", encoding="utf-8")
            (repo / ".github" / "workflows").mkdir(parents=True)
            (repo / ".github" / "workflows" / "ci.yml").write_text("name: CI\n", encoding="utf-8")
            git(repo, "add", ".")
            git(repo, "commit", "-m", "head")
            head_sha = git(repo, "rev-parse", "HEAD")
            (repo / "staged.py").write_text("VALUE = 'staged'\n", encoding="utf-8")
            git(repo, "add", "staged.py")
            (repo / "dirty.py").write_text("VALUE = 'dirty'\n", encoding="utf-8")
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
            diff_stat = (packet_dir / "diff-stat.txt").read_text(encoding="utf-8")
            changed_files = (packet_dir / "changed-files.txt").read_text(encoding="utf-8")
            for scope in ("## committed", "## staged", "## unstaged"):
                self.assertIn(scope, diff_stat)
                self.assertIn(scope, changed_files)
            for path in ("script.py", ".github/workflows/ci.yml", "staged.py", "dirty.py"):
                self.assertIn(path, changed_files)

            script_hunk = (packet_dir / "hunks" / packet.packet_file_name("script.py")).read_text(
                encoding="utf-8"
            )
            staged_hunk = (packet_dir / "hunks" / packet.packet_file_name("staged.py")).read_text(
                encoding="utf-8"
            )
            dirty_hunk = (packet_dir / "hunks" / packet.packet_file_name("dirty.py")).read_text(
                encoding="utf-8"
            )
            self.assertIn("## committed", script_hunk)
            self.assertIn("print('new')", script_hunk)
            self.assertIn("## staged", staged_hunk)
            self.assertIn("VALUE = 'staged'", staged_hunk)
            self.assertIn("## unstaged", dirty_hunk)
            self.assertIn("VALUE = 'dirty'", dirty_hunk)

    def test_scoped_changed_paths_uses_untruncated_name_status(self) -> None:
        packet = import_script()
        with tempfile.TemporaryDirectory() as tmpdir:
            repo = Path(tmpdir)
            git(repo, "init")
            git(repo, "config", "user.email", "agent@example.com")
            git(repo, "config", "user.name", "Agent")
            git(repo, "commit", "--allow-empty", "-m", "base")
            base_sha = git(repo, "rev-parse", "HEAD")
            last_path = ""
            for index in range(70):
                path = (
                    repo
                    / "long"
                    / f"{'a' * 180}_{index:03d}"
                    / f"{'b' * 180}_{index:03d}.py"
                )
                path.parent.mkdir(parents=True)
                path.write_text(f"VALUE = {index}\n", encoding="utf-8")
                last_path = path.relative_to(repo).as_posix()
            git(repo, "add", ".")
            git(repo, "commit", "-m", "many long paths")
            head_sha = git(repo, "rev-parse", "HEAD")
            context = repo / "review-context.json"
            context.write_text(
                json.dumps({"review_scope": {"base_sha": base_sha, "head_sha": head_sha}}),
                encoding="utf-8",
            )

            paths = packet.scoped_changed_paths(repo, packet.diff_scopes(base_sha, head_sha))
            index = packet.write_packet(repo, context, repo / "packet")
            hunk_path = repo / "packet" / "hunks" / packet.packet_file_name(last_path)

            self.assertIn(last_path, paths)
            self.assertTrue(all(not path.startswith("[truncated ") for path in paths))
            self.assertTrue(hunk_path.is_file())
            self.assertLessEqual(len(hunk_path.name), 120)
            self.assertIn(last_path, index.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
