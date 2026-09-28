# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import glob
import re
import sys
import json
import logging
import datetime
import shutil
import shlex
import subprocess
import tempfile
import requests
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from git import Repo, GitCommandError

from opk_ci.license_template_manager import LicenseTemplateManager
from opk_ci.file_utils import FileUtils

logger = logging.getLogger("opk_ci")


class QualityChecks:
    """Class to perform various quality checks on files."""

    # Constants
    JIRA_PROJECTS = ["EXPKITS"]
    CLANG_TIDY_DIAGNOSTIC_RE = re.compile(
        r"^(?:\[[A-Z]+\]\s*)?"
        r"(?P<path>.+?):(?P<line>\d+):(?P<column>\d+):\s+"
        r"(?P<severity>warning|error):\s+"
        r"(?P<message>.+?)\s+\[(?P<check>[A-Za-z0-9_.-]+)\]\s*$")
    CLANG_TIDY_SOURCE_CONTEXT_RE = re.compile(r"^\s*(?:\d+\s*)?\|")
    # TODO: known issue also described here:
    # https://github.com/llvm/llvm-project/pull/111453
    # For future use other zephyr supported static code analysis should be used
    # https://docs.zephyrproject.org/latest/develop/sca/index.html
    CLANG_TIDY_FILTERED_FLAGS = [
        "-fno-reorder-functions",
        "-mfp16-format=ieee",
        "-fno-defer-pop"
    ]
    CLANG_TIDY_PROJECT_FILE_FILTER = (
        r"(^|.*/)(common|config-validator|elements|ops-[^/]+|opk-menu|runtime|tests|web)/.*"
    )
    MERGE_COMMIT_HEADLINE_RE = re.compile(
        r"^Merge (?:(?:(?:remote-tracking )?branch|tag) '[^']+'(?: into .+)?|pull request #\d+\b.*)$",
        re.IGNORECASE,
    )
    COPILOT_AUTOFIX_TRAILER_RE = re.compile(
        r"^Co-authored-by:\s+Copilot Autofix powered by AI <.+@users\.noreply\.github\.com>$",
        re.IGNORECASE,
    )
    JIRA_SUBJECT_PREFIX_RE = re.compile(
        r"^(%s)-\d+\b.+" % "|".join(JIRA_PROJECTS),
        re.IGNORECASE,
    )
    AGENT_RUNTIME_STATIC_TRIGGER_PREFIXES = (
        ".github/agent-runtime/",
        "scripts/private/github_actions.py",
        "scripts/private/github_api.py",
        "scripts/private/agent_runtime/",
        "scripts/private/agent_repair_orchestrator/",
        "scripts/private/agent_stabilization_orchestrator/",
        "scripts/private/agent_workflow_common/",
        "scripts/private/test_support/",
        "scripts/private/tests/",
        ".github/workflows/agent-review.yml",
        ".github/workflows/agent-repair-source-run",
        ".github/workflows/agent-stabilize-pr",
    )
    AGENT_RUNTIME_STATIC_TRIGGER_FILES = (
        "scripts/download-models.py",
        "tools/opk-ci/agent-workflows-mypy.ini",
        "tools/opk-ci/opk_ci/agent_static_analysis.py",
        "tools/opk-ci/opk_ci/config_schema.py",
        "tools/opk-ci/tests/test_agent_static_analysis.py",
        "tools/opk-ci/tests/test_agent_workflow_contracts.py",
        "tools/opk-ci/pyproject.toml",
    )

    def __init__(self):
        self.license_template_manager = LicenseTemplateManager()
        self.file_utils = FileUtils()
        self.autofix_messages = []

    def record_autofix(self, filename, tool, action):
        """Record an in-place fix and emit an actionable message."""
        message = (
            f"{tool} {action} {filename}. "
            f"Review the change, git add {filename}, then rerun the check."
        )
        self.autofix_messages.append(message)
        logger.error(message)

    @staticmethod
    def record_manual_fix(filename, tool, guidance):
        """Emit an actionable message for check-only failures."""
        logger.error(
            f"{tool} requires changes in {filename}. "
            f"{guidance} git add {filename}, then rerun the check."
        )

    def run_in_place_formatter(self, format_cmd, filename, tool, action="reformatted"):
        """Run an in-place formatter and only record an autofix on success."""
        proc = subprocess.run(
            format_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            encoding="utf-8",
        )

        if proc.returncode == 0:
            self.record_autofix(filename, tool, action)
            return True

        logger.error(f"{tool} failed to format {filename}.")
        if proc.stdout:
            logger.error(proc.stdout)
        return False

    @staticmethod
    def log_captured_tool_output(proc_stdout):
        """Log captured formatter output line-by-line for check-only failures."""
        if not proc_stdout:
            return

        for output_line in proc_stdout.rstrip().splitlines():
            logger.error(output_line)

    @staticmethod
    def get_detect_secrets_command():
        """Resolve the detect-secrets hook command from PATH or the active Python."""
        detect_secrets_hook = shutil.which("detect-secrets-hook")
        if detect_secrets_hook:
            return [detect_secrets_hook]

        return [sys.executable, "-m", "detect_secrets.pre_commit_hook"]

    @staticmethod
    def normalize_github_actions_path(filename, project_root):
        if os.path.isabs(filename):
            try:
                filename = os.path.relpath(filename, project_root)
            except ValueError:
                return None

        normalized = filename.replace(os.sep, "/")
        if normalized.startswith("./"):
            normalized = normalized[2:]

        return normalized

    @classmethod
    def normalize_github_actions_workflow(cls, filename, project_root):
        normalized = cls.normalize_github_actions_path(filename, project_root)
        if (
            normalized
            and
            normalized.startswith(".github/workflows/")
            and normalized.endswith((".yml", ".yaml"))
        ):
            return normalized

        return None

    @classmethod
    def is_actionlint_config(cls, filename, project_root):
        return cls.normalize_github_actions_path(filename, project_root) == ".github/actionlint.yaml"

    @staticmethod
    def discover_github_actions_workflows(project_root):
        workflows_dir = os.path.join(project_root, ".github", "workflows")
        workflows = []
        for pattern in ("*.yml", "*.yaml"):
            workflows.extend(
                os.path.relpath(path, project_root).replace(os.sep, "/")
                for path in glob.glob(os.path.join(workflows_dir, pattern))
                if os.path.isfile(path)
            )
        return sorted(workflows)

    def check_github_actions(self, files=None) -> bool:
        """Run actionlint on changed GitHub Actions workflows."""
        logger.info("Checking GitHub Actions workflows with actionlint...")

        files = files or []
        project_root = self.file_utils.get_project_root()
        if any(self.is_actionlint_config(file, project_root) for file in files):
            workflows = self.discover_github_actions_workflows(project_root)
        else:
            workflows = list(dict.fromkeys(
                workflow
                for file in files
                if (workflow := self.normalize_github_actions_workflow(file, project_root))
            ))
        if not workflows:
            logger.info("No GitHub Actions workflow files found to check.")
            return True

        actionlint = shutil.which("actionlint")
        if not actionlint:
            logger.error("actionlint is not available on PATH.")
            return False

        shellcheck = shutil.which("shellcheck")
        if not shellcheck:
            logger.error("shellcheck is not available on PATH.")
            return False

        pyflakes = shutil.which("pyflakes")
        if not pyflakes:
            logger.error("pyflakes is not available on PATH.")
            return False

        cmd = [actionlint, "-shellcheck", shellcheck, "-pyflakes", pyflakes]
        config_file = ".github/actionlint.yaml"
        if os.path.isfile(os.path.join(project_root, config_file)):
            cmd.extend(["-config-file", config_file])
        cmd.extend(workflows)

        proc = subprocess.run(
            cmd,
            cwd=project_root,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            encoding="utf-8",
        )
        if proc.returncode != 0:
            self.log_captured_tool_output(proc.stdout)
            return False

        logger.info("GitHub Actions workflows passed actionlint.")
        return True

    def check_config_schema(self) -> bool:
        """Validate every supported pipeline, Model, and OpChain configuration."""
        from opk_ci.config_schema import run_config_validator

        logger.info("Validating versioned pipeline, Model, and OpChain configurations...")
        project_root = self.file_utils.get_project_root()
        try:
            proc = run_config_validator(project_root)
        except (OSError, ValueError) as error:
            logger.error(str(error))
            return False

        if proc.returncode != 0:
            self.log_captured_tool_output(proc.stdout)
            return False

        logger.info(proc.stdout.strip())
        return True

    @staticmethod
    def iter_file_batches(files, batch_size=50):
        """Yield deterministic file batches to keep secret scans reasonably fast."""
        for start in range(0, len(files), batch_size):
            yield files[start:start + batch_size]

    @staticmethod
    def check_branch_naming() -> bool:
        """Check branch naming convention.
        Returns True if current branch name is valid, False otherwise.
        """
        logger.info("Checking branch naming convention...")

        try:
            repo = Repo(".", search_parent_directories=True)
            branch = repo.active_branch.name
        except Exception as e:
            logger.error(f"Could not get current branch name. {e}")
            return False

        jira_pattern = r"^(?:feature|bugfix|hotfix|release)/(%s)-\d+(?:[-/].+)?$" % "|".join(
            QualityChecks.JIRA_PROJECTS)
        result = False
        if re.match(jira_pattern, branch) or branch in ("main", "develop"):
            result = True
        # automated and sandbox branches
        elif branch.startswith(("dependabot/", "sandbox/")):
            result = True

        if not result:
            logger.error(f"Invalid branch name: \"{branch}\"")
            logger.info("Valid formats:")
            for proj in QualityChecks.JIRA_PROJECTS:
                logger.info(f"  <feature|bugfix|hotfix|release>/{proj}-1234[/ticket-description]")
            logger.info("  sandbox/whatever")
            logger.info(
                "In case of different JIRA project, please update the JIRA_PROJECTS array.")
            logger.info(
                "Please rename your branch accordingly. Hint: git branch -m <new_name>")
        else:
            logger.info(
                f"Branch naming format is valid for \"{branch}\" branch.")

        return result

    @staticmethod
    def filter_comment_lines(commit_msg):
        """Filter out comment lines and empty lines from the commit message."""
        filtered_lines = []
        for line in commit_msg.splitlines():
            stripped_line = line.strip()
            if stripped_line == "# ------------------------ >8 ------------------------":
                break
            if not stripped_line or stripped_line.startswith("#"):
                continue
            filtered_lines.append(stripped_line)
        return filtered_lines

    @staticmethod
    def render_commit_message_for_log(commit_msg, filtered_lines):
        """Render the most actionable commit message content for error logs."""
        if filtered_lines:
            return "\n".join(filtered_lines)

        raw_message = commit_msg.rstrip()
        return raw_message or "<empty>"

    @staticmethod
    def allows_missing_jira_reference(filtered_lines):
        """Allow a narrow set of generated commits to omit the JIRA line."""
        if not filtered_lines or not filtered_lines[0]:
            return False

        if QualityChecks.MERGE_COMMIT_HEADLINE_RE.match(filtered_lines[0]):
            return True

        if len(filtered_lines) < 2:
            return False

        return all(
            QualityChecks.COPILOT_AUTOFIX_TRAILER_RE.match(line)
            for line in filtered_lines[1:]
        )

    @staticmethod
    def check_commit_message(files=None) -> bool:
        """Check commit message format. If a file is provided and looks like a commit message file, read from it."""
        logger.info("Checking commit message format...")
        commit_msg = None

        try:
            repo = Repo(os.getcwd(), search_parent_directories=True)
        except Exception as e:
            logger.error(f"Could not get git repository: {e}")
            return False

        # Try to read from .git/COMMIT_EDITMSG first (the current/pending commit message)
        try:
            commit_msg_file = os.path.join(repo.git_dir, 'COMMIT_EDITMSG')
            logger.info(f"Attempting to read commit message from {commit_msg_file}")
            if os.path.isfile(commit_msg_file):
                with open(commit_msg_file, 'r', encoding='utf-8') as f:
                    commit_msg = f.read()
                    logger.info("Commit message:\n" + commit_msg)
        except Exception as e:
            logger.error(f"Could not read commit message from {commit_msg_file}: {e}")

        # If we couldn't read from COMMIT_EDITMSG, scan passed files and read
        # the first regular file that is found under the .git directory and not ends with a common source code extension.
        if not commit_msg and files and isinstance(files, list):
            git_commit_msg_file = None

            for candidate in files:
                normalized = os.path.normpath(candidate)
                path_parts = normalized.split(os.sep)
                if ".git" in path_parts and not candidate.endswith(('.py', '.cpp', '.c', '.h', '.hpp', '.cmake')):
                    git_commit_msg_file = candidate
                    break

            if git_commit_msg_file:
                try:
                    with open(git_commit_msg_file, 'r', encoding='utf-8') as f:
                        commit_msg = f.read()
                        logger.info(f"Commit message read from {git_commit_msg_file}:\n" + commit_msg)
                except Exception as e:
                    logger.error(
                        f"Could not read commit message file {git_commit_msg_file}: {e}")
                    return False

        # If still no commit message, use empty string to make sure the check fails
        if not commit_msg:
            commit_msg = ""

        filtered_lines = QualityChecks.filter_comment_lines(commit_msg)

        if len(filtered_lines) < 2:
            if not filtered_lines:
                logger.error(
                    "Commit message must have at least two lines: a description and a reference to a JIRA ticket.")
                logger.info("Example:")
                logger.info("  Add new feature for X\n  Task: EXPKITS-4242")
                logger.info(
                    "The current commit message is:\n"
                    + QualityChecks.render_commit_message_for_log(commit_msg, filtered_lines))
                logger.info(
                    "Please rename your commit accordingly. Hint: git commit --amend")
                return False

            if QualityChecks.allows_missing_jira_reference(filtered_lines):
                logger.info("Commit message format is valid.")
                return True

            logger.error(
                "Commit message must have at least two lines: a description and a reference to a JIRA ticket.")
            logger.info("Example:")
            logger.info("  Add new feature for X\n  Task: EXPKITS-4242")
            logger.info(
                "The current commit message is:\n"
                + QualityChecks.render_commit_message_for_log(commit_msg, filtered_lines))
            logger.info(
                "Please rename your commit accordingly. Hint: git commit --amend")
            return False

        if not filtered_lines[0]:
            logger.error(
                "First line of commit message must be a non-empty description.")
            logger.info(
                "The current commit message is:\n"
                + QualityChecks.render_commit_message_for_log(commit_msg, filtered_lines))
            return False

        if QualityChecks.allows_missing_jira_reference(filtered_lines):
            logger.info("Commit message format is valid.")
            return True

        # Second line: <bug|task>: JIRA-XXXX
        jira_pattern = r"^(Bug|Task): (%s)-\d+$" % "|".join(QualityChecks.JIRA_PROJECTS)
        if not re.match(jira_pattern, filtered_lines[1], re.IGNORECASE):
            logger.error(
                f"Second line must match \"<Bug|Task>: JIRA-XXXX\" with a valid JIRA project.")
            logger.info("Example:")
            logger.info(f"  Task: {QualityChecks.JIRA_PROJECTS[0]}-1234")
            logger.info(
                "The current commit message is:\n"
                + QualityChecks.render_commit_message_for_log(commit_msg, filtered_lines))
            return False

        logger.info("Commit message format is valid.")
        return True

    @staticmethod
    def _check_commit_message_on_ci(commit, jira_pattern) -> bool:
        sha = commit.hexsha[:8]
        filtered_lines = QualityChecks.filter_comment_lines(commit.message)

        if filtered_lines and QualityChecks.JIRA_SUBJECT_PREFIX_RE.match(filtered_lines[0]):
            logger.info(f"[{sha}] Commit message format is valid.")
            return True

        if len(filtered_lines) < 2:
            if not filtered_lines:
                logger.error(
                    f"[{sha}] Commit message must have at least two lines: "
                    "a description and a reference to a JIRA ticket.")
                logger.error("Example:")
                logger.error("  Add new feature for X\n  Task: EXPKITS-4242")
                logger.error(
                    "The current commit message is:\n"
                    + QualityChecks.render_commit_message_for_log(commit.message, filtered_lines))
                return False

            if QualityChecks.allows_missing_jira_reference(filtered_lines):
                logger.info(f"[{sha}] Commit message format is valid.")
                return True
            logger.error(
                f"[{sha}] Commit message must have at least two lines: "
                "a description and a reference to a JIRA ticket.")
            logger.error("Example:")
            logger.error("  Add new feature for X\n  Task: EXPKITS-4242")
            logger.error(
                "The current commit message is:\n"
                + QualityChecks.render_commit_message_for_log(commit.message, filtered_lines))
            return False

        if not filtered_lines[0]:
            logger.error(f"[{sha}] First line of commit message must be a non-empty description.")
            logger.error(
                "The current commit message is:\n"
                + QualityChecks.render_commit_message_for_log(commit.message, filtered_lines))
            return False

        if QualityChecks.allows_missing_jira_reference(filtered_lines):
            logger.info(f"[{sha}] Commit message format is valid.")
            return True

        if not re.match(jira_pattern, filtered_lines[1], re.IGNORECASE):
            logger.error(
                f"[{sha}] Second line must match \"<Bug|Task>: JIRA-XXXX\" with a valid JIRA project.")
            logger.error("Example:")
            logger.error(f"  Task: {QualityChecks.JIRA_PROJECTS[0]}-1234")
            logger.error(
                "The current commit message is:\n"
                + QualityChecks.render_commit_message_for_log(commit.message, filtered_lines))
            return False

        logger.info(f"[{sha}] Commit message format is valid.")
        return True

    @staticmethod
    def check_commit_messages_on_ci(files=None, target_branch=None) -> bool:
        """Check all commit messages on the current branch that are not on target_branch.

        When target_branch is provided the set of commits checked is those
        introduced after its merge base. For main and develop targets, prefer
        the other protected branch only when its merge base is newer; otherwise
        use the target branch.
        When target_branch is omitted, only HEAD is checked.
        """
        logger.info("Checking commit message format...")

        try:
            repo = Repo(os.getcwd(), search_parent_directories=True)
        except Exception as e:
            logger.error(f"Could not get git repository: {e}")
            return False

        # Collect the commits to validate.
        commits = []
        if target_branch:
            try:
                # In CI the checkout is detached, so the bare branch name (e.g. "main")
                # does not exist as a local ref. Prefer "origin/<branch>" and fall back
                # to the bare name so the function works both locally and on CI.
                remote_ref = f"origin/{target_branch}"
                try:
                    target_commit = repo.commit(remote_ref)
                    logger.info(f"Resolved target branch as '{remote_ref}'")
                except (GitCommandError, Exception):
                    target_commit = repo.commit(target_branch)
                    logger.info(f"Resolved target branch as '{target_branch}'")

                validation_ref = remote_ref
                merge_base_list = repo.merge_base(repo.head.commit, target_commit)
                if not merge_base_list:
                    logger.error(f"Could not find merge base between HEAD and '{validation_ref}'.")
                    return False
                merge_base = merge_base_list[0]

                if target_branch in ("main", "develop"):
                    alternate_branch = "develop" if target_branch == "main" else "main"
                    alternate_ref = f"origin/{alternate_branch}"
                    try:
                        repo.git.fetch(
                            "origin",
                            f"+refs/heads/{alternate_branch}:refs/remotes/{alternate_ref}",
                        )
                        alternate_commit = repo.commit(alternate_ref)
                        alternate_base_list = repo.merge_base(repo.head.commit, alternate_commit)
                        if not alternate_base_list:
                            logger.warning(
                                f"No merge base found for {alternate_ref}; using {validation_ref}")
                        elif (alternate_base_list[0].hexsha != merge_base.hexsha
                              and repo.is_ancestor(merge_base, alternate_base_list[0])):
                            merge_base = alternate_base_list[0]
                            validation_ref = alternate_ref
                    except Exception as exc:
                        logger.warning(
                            f"Could not compare against {alternate_ref}; using {validation_ref}: {exc}")

                logger.info(
                    f"Checking commits after {validation_ref} merge base {merge_base.hexsha[:8]}")
                for commit in repo.iter_commits(f"{merge_base.hexsha}..HEAD"):
                    commits.append(commit)
            except GitCommandError as e:
                logger.error(f"Could not determine commits relative to '{target_branch}': {e}")
                return False
            except Exception as e:
                logger.error(f"Unexpected error collecting commits: {e}")
                return False
        else:
            try:
                commits = [repo.head.commit]
            except Exception as e:
                logger.error(f"Could not read HEAD commit: {e}")
                return False

        if not commits:
            logger.info("No commits to check.")
            return True

        logger.info(f"Checking {len(commits)} commit(s)...")

        jira_pattern = r"^(Bug|Task): (%s)-\d+$" % "|".join(QualityChecks.JIRA_PROJECTS)
        results = [
            QualityChecks._check_commit_message_on_ci(commit, jira_pattern)
            for commit in commits
        ]
        return all(results)

    @staticmethod
    def get_http_response(url, timeout=10):
        """Perform a HTTP GET request and return the response object."""
        response = None
        try:
            response = requests.get(url, timeout=timeout)
        except Exception as e:
            logger.error(f"HTTP request to {url} failed: {e}")
            raise

        return response

    @staticmethod
    def check_jira_ticket() -> bool:
        """Checks if jira ticket mentioned in branch exists and in progress."""
        # TODO: needs github side authentication
        logger.info("Checking JIRA ticket references...")
        result = True

        try:
            repo = Repo(".", search_parent_directories=True)
            branch = repo.active_branch.name
        except Exception as e:
            logger.error(f"Could not get current branch name. {e}")
            return False

        ticket_uri = "https://jira.arm.com/rest/api/2/issue/" + branch.split('/')[1] + "?fields=status"
        logger.info(f"Checking JIRA ticket: {ticket_uri}")
        try:
            ticket_status = QualityChecks.get_http_response(ticket_uri).json()["fields"]["status"]["name"]
            allowed_statuses = ["In Progress"]
            if not any(status in ticket_status for status in allowed_statuses):
                logger.error(f"JIRA ticket {ticket_uri} is not in progress.")
                result = False
        except Exception as e:
            logger.error(f"Error checking JIRA ticket {ticket_uri}: {e}")
            result = False

        if result:
            logger.info("All files have valid JIRA ticket references.")

        return result

    def check_secrets(self, files=None, baseline=".secrets.baseline") -> bool:
        """Check for secrets in the given files or all tracked files if none specified."""
        logger.info("Checking for secrets...")

        result = True

        if not os.path.isfile(baseline):
            logger.error(f"Baseline file {baseline} not found!")
            return False
        if files is None:
            # No explicit file set was provided, so scan all git-tracked files.
            try:
                git_ls_files = subprocess.run(["git", "ls-files"], capture_output=True, text=True, check=True)
                files = git_ls_files.stdout.strip().splitlines()
            except Exception as e:
                logger.error(f"Failed to get git-tracked files: {e}")
                result = False
                return result

        existing_files = [file for file in files if os.path.isfile(file)]
        detect_secrets_command = self.get_detect_secrets_command()

        for file_batch in self.iter_file_batches(existing_files):
            cmd = [*detect_secrets_command, "--baseline", baseline, *file_batch]
            proc = subprocess.run(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                encoding="utf-8",
            )
            if proc.returncode != 0:
                logger.error("Secrets detected in scanned files.")
                if proc.stdout:
                    for output_line in proc.stdout.rstrip().splitlines():
                        logger.error(output_line)
                result = False
        if result:
            logger.info("No secrets detected.")
        return result

    @classmethod
    def is_agent_runtime_static_file(cls, filename):
        normalized = filename.replace(os.sep, "/")
        return normalized in cls.AGENT_RUNTIME_STATIC_TRIGGER_FILES or any(
            normalized.startswith(prefix)
            for prefix in cls.AGENT_RUNTIME_STATIC_TRIGGER_PREFIXES
        )

    @classmethod
    def should_run_agent_runtime_static_analysis(cls, files):
        return any(cls.is_agent_runtime_static_file(filename) for filename in files or [])

    @staticmethod
    def name_status_paths(name_status_output):
        tokens = [token for token in name_status_output.split("\0") if token]
        paths = []
        index = 0
        while index < len(tokens):
            status = tokens[index]
            index += 1
            if status.startswith("R") or status.startswith("C"):
                if index + 1 >= len(tokens):
                    break
                paths.extend([tokens[index], tokens[index + 1]])
                index += 2
                continue
            if index >= len(tokens):
                break
            paths.append(tokens[index])
            index += 1
        return paths

    @classmethod
    def should_run_agent_runtime_static_analysis_for_name_status_command(cls, command, failure_message):
        proc = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            encoding="utf-8",
        )
        if proc.returncode != 0:
            if proc.stdout:
                cls.log_captured_tool_output(proc.stdout)
            logger.error(failure_message)
            return True
        return cls.should_run_agent_runtime_static_analysis(cls.name_status_paths(proc.stdout))

    @classmethod
    def should_run_agent_runtime_static_analysis_for_base_ref(cls, pr_target_branch):
        return cls.should_run_agent_runtime_static_analysis_for_name_status_command(
            ["git", "diff", "--name-status", "-z", f"origin/{pr_target_branch}...HEAD"],
            "Could not inspect PR diff for Agent runtime static analysis.",
        )

    @staticmethod
    def check_agent_runtime_static_analysis(files=None, pr_target_branch=None) -> bool:
        """Run the shared Agent runtime static analysis gate when relevant files changed."""
        files = files or []
        should_run = QualityChecks.should_run_agent_runtime_static_analysis(files)
        if not should_run and pr_target_branch:
            should_run = QualityChecks.should_run_agent_runtime_static_analysis_for_base_ref(pr_target_branch)
        if not should_run:
            logger.info("No Agent runtime files found for static analysis.")
            return True

        logger.info("Running Agent workflow static analysis...")
        project_root = FileUtils.get_project_root()
        command = [sys.executable, "-m", "opk_ci.agent_static_analysis"]
        if pr_target_branch:
            command.extend(["--base-ref", f"origin/{pr_target_branch}"])

        environment = os.environ.copy()
        opk_ci_root = os.path.join(project_root, "tools", "opk-ci")
        environment["PYTHONPATH"] = (
            opk_ci_root
            if not environment.get("PYTHONPATH")
            else os.pathsep.join([opk_ci_root, environment["PYTHONPATH"]])
        )
        proc = subprocess.run(
            command,
            cwd=project_root,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            encoding="utf-8",
        )
        if proc.returncode != 0:
            if proc.stdout:
                QualityChecks.log_captured_tool_output(proc.stdout)
            logger.error("Agent workflow static analysis failed.")
            return False
        if proc.stdout:
            for output_line in proc.stdout.rstrip().splitlines():
                logger.info(output_line)

        logger.info("Agent workflow static analysis passed.")
        return True

    def check_clang_format(self, files, format, verbose=False) -> bool:
        """Check clang-format validity to files under folder using clang-format."""
        logger.info("Checking clang-format validity...")

        files = self.file_utils.filter_by_path_ending(
            files, self.file_utils.file_endings["cpp"])
        result = True

        if not files:
            logger.info("No C/C++ files found to check.")
            return result

        for filename in files:
            cmd = ["clang-format", "--dry-run", "--Werror", filename]
            if verbose:
                cmd.append("--verbose")

            try:
                proc = subprocess.run(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    encoding="utf-8",
                )

                if proc.returncode != 0:
                    result = False
                    if format:
                        format_cmd = ["clang-format", "-i", filename]
                        self.run_in_place_formatter(
                            format_cmd, filename, "clang-format")
                    else:
                        self.log_captured_tool_output(proc.stdout)
                        self.record_manual_fix(
                            filename,
                            "clang-format",
                            "Reformat the file with clang-format.",
                        )
            except Exception as e:
                result = False
                logger.error(f"Error writing output to {filename}: {e}")

        if result:
            logger.info("All files passed clang-format check.")

        return result

    @staticmethod
    def _resolve_clang_tidy_binary(clang_tidy_binary=None):
        """Resolve clang-tidy from an override, PATH, or the active Python environment."""
        if clang_tidy_binary:
            return clang_tidy_binary

        clang_tidy = shutil.which("clang-tidy")
        if clang_tidy:
            return clang_tidy

        venv_clang_tidy = os.path.join(os.path.dirname(sys.executable), "clang-tidy")
        if os.path.isfile(venv_clang_tidy) and os.access(venv_clang_tidy, os.X_OK):
            return venv_clang_tidy

        return None

    def _resolve_compile_commands_dir(self, compile_commands_dir=None):
        """Resolve the compile database directory used by clang-tidy."""
        project_root = self.file_utils.get_project_root()

        if compile_commands_dir:
            if os.path.isabs(compile_commands_dir):
                candidate_dir = compile_commands_dir
            else:
                candidate_dir = os.path.join(project_root, compile_commands_dir)
        else:
            candidate_dir = os.path.join(project_root, "development", "build")

        compile_commands_path = os.path.join(candidate_dir, "compile_commands.json")
        if os.path.isfile(compile_commands_path):
            logger.info(f"Using clang-tidy compile database: {compile_commands_path}")
            return candidate_dir

        logger.error("Could not find compile_commands.json for clang-tidy.")
        logger.error(f"Checked path: {compile_commands_path}")
        logger.error("Build the project first, for example with: ./scripts/build.sh debug true")
        return None

    @staticmethod
    def _filter_compile_command_flags(compile_commands_path, output_dir, flags):
        """Write a filtered compile_commands.json copy for clang-tidy compatibility."""
        filtered_compile_commands_path = os.path.join(output_dir, "compile_commands.json")

        with open(compile_commands_path, 'r', encoding='utf-8') as f:
            compile_commands = json.load(f)

        flags_to_remove = set(flags)
        for entry in compile_commands:
            if "arguments" in entry:
                entry["arguments"] = [
                    arg for arg in entry["arguments"]
                    if arg not in flags_to_remove
                ]
            if "command" in entry:
                command = shlex.split(entry["command"])
                command = [
                    arg for arg in command
                    if arg not in flags_to_remove
                ]
                entry["command"] = shlex.join(command)

        with open(filtered_compile_commands_path, 'w', encoding='utf-8') as f:
            json.dump(compile_commands, f, indent=2)

        logger.info(f"Prepared filtered clang-tidy compile database: {filtered_compile_commands_path}")
        return output_dir

    @staticmethod
    def _compile_database_files(compile_commands_path, project_root):
        """Return files covered by the current compile database."""
        with open(compile_commands_path, 'r', encoding='utf-8') as f:
            compile_commands = json.load(f)

        compiled_files = set()
        project_root = os.path.realpath(project_root)

        for entry in compile_commands:
            file_path = entry.get("file")
            directory = entry.get("directory")
            if not file_path or not directory:
                continue

            if not os.path.isabs(file_path):
                file_path = os.path.join(directory, file_path)

            file_path = os.path.realpath(file_path)
            try:
                rel_path = os.path.relpath(file_path, project_root)
            except ValueError:
                continue

            compiled_files.add(os.path.normpath(rel_path))

        return compiled_files

    def _clang_tidy_files_in_compile_database(
            self, files, compile_commands_path, project_root):
        """Return requested files covered by the current compile database."""
        compiled_files = self._compile_database_files(
            compile_commands_path, project_root)
        normalized_files = []
        for f in files:
            abs_path = f if os.path.isabs(f) else os.path.join(project_root, f)
            abs_path = os.path.realpath(abs_path)
            try:
                rel_path = os.path.normpath(os.path.relpath(abs_path, project_root))
            except ValueError:
                continue
            normalized_files.append((f, abs_path, rel_path))

        skipped_files = sorted({
            orig for (orig, _abs, rel_path) in normalized_files
            if rel_path not in compiled_files
        })
        if skipped_files:
            logger.info(
                "Skipping %d C/C++ file(s) not listed in the active compile database.",
                len(skipped_files))
            for skipped_file in skipped_files:
                logger.debug(f"Skipped clang-tidy file not in compile database: {skipped_file}")

        return [
            abs_path for (_orig, abs_path, rel_path) in normalized_files
            if rel_path in compiled_files
        ]

    @staticmethod
    def _run_clang_tidy_files(
            files, clang_tidy, filtered_compile_config_path, project_root):
        """Run clang-tidy in parallel and report progress."""
        project_clang_tidy_config = os.path.join(project_root, ".clang-tidy")
        config_file_args = []
        # Pin the root config so nested files cannot shadow the project policy.
        if os.path.isfile(project_clang_tidy_config):
            config_file_args = [f"--config-file={project_clang_tidy_config}"]
        else:
            logger.warning(
                "No .clang-tidy config file found at project root; "
                "HeaderFilterRegex may not apply.")

        # Keep diagnostics scoped to project sources, including analyzer notes.
        line_filter_arg = "--line-filter=" + json.dumps([
            {"name": QualityChecks.CLANG_TIDY_PROJECT_FILE_FILTER}
        ])

        def run_clang_tidy(f):
            try:
                cmd = [
                    clang_tidy,
                    f,
                    "-p",
                    filtered_compile_config_path,
                    *config_file_args,
                    line_filter_arg,
                    "--extra-arg=-DFMT_CONSTEVAL="
                ]
                return f, subprocess.run(
                    cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding="utf-8"), None
            except Exception as e:
                return f, None, e

        result = True
        workers = min(len(files), os.cpu_count() or 1)
        logger.info(
            "Running clang-tidy on %d file(s) with %d worker(s).",
            len(files), workers)
        with ThreadPoolExecutor(max_workers=workers) as executor:
            for completed, (f, proc, error) in enumerate(
                    executor.map(run_clang_tidy, files), start=1):
                if error:
                    logger.error(f"Failed to run clang-tidy on {f}: {error}")
                    result = False
                elif proc.returncode != 0:
                    logger.error(f"clang-tidy check failed for {f}.")
                    logger.error(proc.stdout)
                    logger.error(proc.stderr)
                    result = False
                elif proc.stdout:
                    logger.debug(f"clang-tidy output for {f}:\n{proc.stdout}")
                logger.info(
                    "clang-tidy progress: %d/%d file(s) completed.",
                    completed, len(files))

        return result

    def check_clang_tidy(self, files, compile_commands_dir=None, clang_tidy_binary=None) -> bool:
        """Check clang-tidy validity to files under folder."""
        logger.info("Checking clang-tidy validity...")

        files = self.file_utils.filter_by_path_ending(
            files, self.file_utils.file_endings["cpp"])
        result = True

        if not files:
            logger.info("No C/C++ files found to check with clang-tidy.")
            return result

        clang_tidy = self._resolve_clang_tidy_binary(clang_tidy_binary)
        if not clang_tidy:
            logger.error("Could not find clang-tidy. Install it or pass --clang-tidy-binary.")
            return False

        compile_config_path = self._resolve_compile_commands_dir(compile_commands_dir)
        if not compile_config_path:
            return False
        compile_commands_path = os.path.join(compile_config_path, "compile_commands.json")
        project_root = self.file_utils.get_project_root()

        try:
            files = self._clang_tidy_files_in_compile_database(
                files,
                compile_commands_path, project_root)
        except Exception as e:
            logger.error(f"Failed to read clang-tidy compile database files: {e}")
            return False

        if not files:
            logger.info("No C/C++ files listed in the active compile database.")
            return result

        with tempfile.TemporaryDirectory(prefix="opk-clang-tidy-") as filtered_compile_config_path:
            try:
                filtered_compile_config_path = self._filter_compile_command_flags(
                    compile_commands_path, filtered_compile_config_path, self.CLANG_TIDY_FILTERED_FLAGS)
            except Exception as e:
                logger.error(f"Failed to prepare clang-tidy compile database: {e}")
                return False

            result = self._run_clang_tidy_files(
                files, clang_tidy, filtered_compile_config_path, project_root)

        if result:
            logger.info("All files passed clang-tidy check.")

        return result

    @staticmethod
    def _count_clang_tidy_diagnostics(diagnostics):
        """Count parsed clang-tidy diagnostics by check name and severity."""
        check_counts = Counter()
        severity_counts = Counter()

        for diagnostic in diagnostics:
            severity_counts[diagnostic["severity"]] += 1
            check_counts[diagnostic["check"]] += 1

        return check_counts, severity_counts

    @classmethod
    def parse_clang_tidy_diagnostics(cls, log_file):
        """Parse clang-tidy diagnostics (currently for errors and warnings) with their locations and source excerpts."""
        diagnostics = []

        with open(log_file, 'r', encoding='utf-8') as f:
            lines = f.readlines()

        for line_index, raw_line in enumerate(lines):
            match = cls.CLANG_TIDY_DIAGNOSTIC_RE.match(raw_line.rstrip())
            if not match:
                continue

            source_context = []
            for context_line in lines[line_index + 1:]:
                context_line = context_line.rstrip()
                if cls.CLANG_TIDY_SOURCE_CONTEXT_RE.match(context_line):
                    source_context.append(context_line)
                    continue
                break

            diagnostics.append({
                "path": match.group("path"),
                "line": int(match.group("line")),
                "column": int(match.group("column")),
                "severity": match.group("severity"),
                "message": match.group("message"),
                "check": match.group("check"),
                "source_context": source_context,
            })

        return diagnostics

    @classmethod
    def parse_clang_tidy_statistics(cls, log_file):
        """Parse clang-tidy diagnostics from a log file and count them by check name."""
        diagnostics = cls.parse_clang_tidy_diagnostics(log_file)

        return cls._count_clang_tidy_diagnostics(diagnostics)

    @staticmethod
    def clang_tidy_statistics_to_dict(log_file, check_counts, severity_counts):
        """Convert parsed clang-tidy statistics into a stable JSON structure."""
        return {
            "schema": 1,
            "source": log_file,
            "generated_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "total": sum(check_counts.values()),
            "levels": dict(sorted(severity_counts.items())),
            "checks": dict(sorted(check_counts.items())),
        }

    @staticmethod
    def write_clang_tidy_statistics_json(stats, output_file):
        """Write clang-tidy statistics to JSON."""
        output_dir = os.path.dirname(output_file)
        if output_dir:
            os.makedirs(output_dir, exist_ok=True)

        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump(stats, f, indent=2, sort_keys=True)
            f.write("\n")

        logger.info("Wrote clang-tidy statistics JSON: %s", output_file)

    @staticmethod
    def _load_clang_tidy_baseline(baseline_file):
        """Load accepted per-check clang-tidy baseline counts."""
        with open(baseline_file, 'r', encoding='utf-8') as f:
            baseline = json.load(f)

        checks = baseline.get("checks")
        if not isinstance(checks, dict):
            raise ValueError("Baseline JSON must contain a 'checks' object.")

        return baseline, {check: int(count) for check, count in checks.items()}

    @staticmethod
    def _compare_clang_tidy_checks(current_checks, baseline_checks):
        """Compare current clang-tidy counts to accepted baseline counts."""
        regressions = []
        improvements = []
        unchanged = []

        for check in sorted(set(baseline_checks) | set(current_checks)):
            accepted = baseline_checks.get(check, 0)
            current = current_checks.get(check, 0)
            delta = current - accepted
            item = {
                "check": check,
                "accepted": accepted,
                "current": current,
                "delta": delta,
            }
            if delta > 0:
                regressions.append(item)
            elif delta < 0:
                improvements.append(item)
            else:
                unchanged.append(item)

        regressions.sort(key=lambda item: item["delta"], reverse=True)
        improvements.sort(key=lambda item: item["delta"])
        return regressions, improvements, unchanged

    @staticmethod
    def _log_clang_tidy_regression_diagnostics(regressions, diagnostics):
        """Connect each regressed check count to the diagnostics that produced it."""
        logger.error("")
        logger.error("Source diagnostics for regressed checks:")

        for item in regressions:
            matching_diagnostics = [
                diagnostic for diagnostic in diagnostics
                if diagnostic["check"] == item["check"]
            ]
            if not matching_diagnostics:
                logger.error(
                    "  %s: no matching source diagnostics were parsed from the clang-tidy log.",
                    item["check"])
                continue

            if item["accepted"] == 0:
                logger.error(
                    "  %s: the following %d diagnostic(s) account for the %+d regression:",
                    item["check"], len(matching_diagnostics), item["delta"])
            else:
                logger.error(
                    "  %s: the baseline stores counts only, so it cannot identify which %+d "
                    "diagnostic(s) are new; showing all %d current diagnostic(s):",
                    item["check"], item["delta"], len(matching_diagnostics))

            for diagnostic in matching_diagnostics:
                logger.error(
                    "    %s:%d:%d: %s: %s [%s]",
                    diagnostic["path"],
                    diagnostic["line"],
                    diagnostic["column"],
                    diagnostic["severity"],
                    diagnostic["message"],
                    diagnostic["check"],
                )
                for context_line in diagnostic["source_context"]:
                    logger.error("    %s", context_line)

    @staticmethod
    def _log_clang_tidy_baseline_comparison(
            baseline_file, mode, regressions, improvements, diagnostics=None):
        """Log clang-tidy baseline comparison results."""
        logger.info("clang-tidy baseline comparison:")
        logger.info("  baseline: %s", baseline_file)
        logger.info("  mode: %s", mode)
        if regressions:
            logger.error("  result: FAILED (%d per-check regression(s))", len(regressions))
            logger.error("%-55s %8s %8s %8s", "check", "accepted", "current", "delta")
            logger.error("%-55s %8s %8s %8s", "-" * 55, "--------", "-------", "-----")
            for item in regressions:
                logger.error("%-55s %8d %8d %+8d",
                             item["check"], item["accepted"], item["current"], item["delta"])
            QualityChecks._log_clang_tidy_regression_diagnostics(
                regressions, diagnostics or [])
        else:
            logger.info("  result: PASSED")

        if improvements:
            logger.info("  improvements: %d check(s) below accepted baseline", len(improvements))

    @staticmethod
    def compare_clang_tidy_statistics_to_baseline(
            stats, baseline_file, mode="advisory", diagnostics=None):
        """Compare current clang-tidy per-check counts to an accepted baseline."""
        if not os.path.isfile(baseline_file):
            logger.error("Could not find clang-tidy baseline file: %s", baseline_file)
            return False

        try:
            _, baseline_checks = QualityChecks._load_clang_tidy_baseline(baseline_file)
        except Exception as e:
            logger.error("Failed to load clang-tidy baseline file: %s", e)
            return False

        current_checks = {check: int(count) for check, count in stats.get("checks", {}).items()}
        regressions, improvements, _ = QualityChecks._compare_clang_tidy_checks(current_checks, baseline_checks)
        QualityChecks._log_clang_tidy_baseline_comparison(
            baseline_file, mode, regressions, improvements, diagnostics)

        if mode == "enforce" and regressions:
            return False

        return True

    @staticmethod
    def update_clang_tidy_baseline(stats, baseline_file, diagnostics=None):
        """Update baseline to current counts only when no per-check count regresses."""
        if not os.path.isfile(baseline_file):
            logger.error("Could not find clang-tidy baseline file: %s", baseline_file)
            return False

        try:
            _, baseline_checks = QualityChecks._load_clang_tidy_baseline(baseline_file)
        except Exception as e:
            logger.error("Failed to load clang-tidy baseline file: %s", e)
            return False

        current_checks = {check: int(count) for check, count in stats.get("checks", {}).items() if int(count) > 0}
        regressions, improvements, _ = QualityChecks._compare_clang_tidy_checks(current_checks, baseline_checks)
        QualityChecks._log_clang_tidy_baseline_comparison(
            baseline_file, "update-baseline", regressions, improvements, diagnostics)

        if regressions:
            logger.error("Refusing to update clang-tidy baseline because current counts exceed the existing baseline.")
            return False

        output_dir = os.path.dirname(baseline_file)
        if output_dir:
            os.makedirs(output_dir, exist_ok=True)

        with open(baseline_file, 'w', encoding='utf-8') as f:
            json.dump({"checks": dict(sorted(current_checks.items()))}, f, indent=2, sort_keys=True)
            f.write("\n")

        logger.info("Updated clang-tidy baseline: %s", baseline_file)
        return True

    @staticmethod
    def report_clang_tidy_statistics(log_file, stats_output=None, baseline_file=None,
                                     baseline_mode="advisory", update_baseline=False) -> bool:
        """Report clang-tidy diagnostic counts by check name from a log file."""
        logger.info("Creating clang-tidy statistics from: %s", log_file)

        if not os.path.isfile(log_file):
            logger.error("Could not find clang-tidy log file: %s", log_file)
            return False

        try:
            diagnostics = QualityChecks.parse_clang_tidy_diagnostics(log_file)
            check_counts, severity_counts = QualityChecks._count_clang_tidy_diagnostics(diagnostics)
        except Exception as e:
            logger.error("Failed to parse clang-tidy log file: %s", e)
            return False

        stats = QualityChecks.clang_tidy_statistics_to_dict(log_file, check_counts, severity_counts)
        total_count = stats["total"]
        if total_count == 0:
            logger.info("No clang-tidy diagnostics found.")
        else:
            logger.info("clang-tidy diagnostics summary:")
            logger.info("  total: %d", total_count)
            for severity, count in sorted(severity_counts.items()):
                logger.info("  %s: %d", severity, count)

            logger.info("")
            logger.info("%-55s %8s", "check", "count")
            logger.info("%-55s %8s", "-" * 55, "-----")
            for check_name, count in check_counts.most_common():
                logger.info("%-55s %8d", check_name, count)

        if stats_output:
            try:
                QualityChecks.write_clang_tidy_statistics_json(stats, stats_output)
            except Exception as e:
                logger.error("Failed to write clang-tidy statistics JSON: %s", e)
                return False

        if update_baseline:
            if not baseline_file:
                logger.error("--clang-tidy-update-baseline requires --clang-tidy-baseline.")
                return False
            return QualityChecks.update_clang_tidy_baseline(
                stats, baseline_file, diagnostics=diagnostics)

        if baseline_file:
            return QualityChecks.compare_clang_tidy_statistics_to_baseline(
                stats, baseline_file, mode=baseline_mode, diagnostics=diagnostics)

        return True

    def check_python_format(self, files, format, verbose=False) -> bool:
        """Check PEP-8 compliance for Python files."""
        logger.info("Checking Python files for PEP-8 compliance...")

        files = self.file_utils.filter_by_path_ending(
            files, self.file_utils.file_endings["py"])
        result = True

        if not files:
            logger.info("No Python files found to check.")
            return result

        project_root = self.file_utils.get_project_root()
        for f in files:
            try:

                cmd = [sys.executable, "-m", "autopep8", "--diff", f]
                proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                      encoding="utf-8", cwd=project_root)

                if proc.stdout.strip():
                    result = False

                    if format:
                        cmd = [sys.executable, "-m", "autopep8",  "--in-place", f]
                        if verbose:
                            cmd.append("--verbose")
                        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                              encoding="utf-8", cwd=project_root)

                        if proc.returncode == 0:
                            self.record_autofix(f, "autopep8", "reformatted")
                            result = False
                        else:
                            logger.error(f"autopep8 failed to format {f}.")
                            self.log_captured_tool_output(proc.stdout)
                            self.log_captured_tool_output(proc.stderr)
                            result = False
                    else:
                        self.record_manual_fix(
                            f,
                            "autopep8",
                            "Reformat the file to match PEP-8.",
                        )
                        self.log_captured_tool_output(proc.stdout)
                        result = False
                elif proc.returncode != 0:
                    logger.error(f"autopep8 check failed for {f}.")
                    self.log_captured_tool_output(proc.stdout)
                    self.log_captured_tool_output(proc.stderr)
                    result = False
            except subprocess.CalledProcessError as e:
                logger.error(f"Error running autopep8 on {f}: {e}")
                result = False
            except Exception as e:
                logger.error(f"Unknown error running autopep8 on {f}: {e}")
                result = False

        if result:
            logger.info("All files passed PEP-8 (autopep8) check.")

        return result

    def check_cmake_format(self, files, format, verbose=False) -> bool:
        """Format CMake files."""
        logger.info("Checking CMake files for formatting...")

        result = True
        files = self.file_utils.filter_by_path_ending(
            files, self.file_utils.file_endings["cmake"])

        if not files:
            logger.info("No CMake files found to check.")
            return result

        for filename in files:
            cmd = [
                "cmake-format",
                "-c",
                ".cmake-format.yaml",  # NOSONAR: keep the repo-root config literal inline for cmake-format.
                "--check",
                filename,
            ]

            try:
                proc = subprocess.run(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    encoding="utf-8",
                )

                if proc.returncode != 0:
                    result = False
                    if format:
                        format_cmd = [
                            "cmake-format",
                            "-c",
                            ".cmake-format.yaml",  # NOSONAR: keep the repo-root config literal inline for cmake-format.
                            "-i",
                            filename,
                        ]
                        self.run_in_place_formatter(
                            format_cmd, filename, "cmake-format")
                    else:
                        self.log_captured_tool_output(proc.stdout)
                        self.record_manual_fix(
                            filename,
                            "cmake-format",
                            "Reformat the file with cmake-format.",
                        )
            except Exception as e:
                logger.error(f"Error writing output to {filename}: {e}")
                result = False

        if result:
            logger.info("All CMake files passed formatting check.")

        return result

    def check_shell_format(self, files, format) -> bool:
        """Check shell script files for formatting."""
        logger.info("Checking shell script files for formatting...")

        result = True
        files = self.file_utils.filter_by_path_ending(
            files, self.file_utils.file_endings["sh"])

        if not files:
            logger.info("No shell script files found to check.")
            return result

        for filename in files:
            shfmt_args = ["-i", "4", "-ci", "-sr", "-kp"]
            cmd = ["shfmt", *shfmt_args, "-d", filename]
            try:
                proc = subprocess.run(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    encoding="utf-8",
                )

                if proc.returncode != 0:
                    result = False
                    if format:
                        format_cmd = ["shfmt", *shfmt_args, "-w", filename]
                        self.run_in_place_formatter(
                            format_cmd, filename, "shfmt")
                    else:
                        self.log_captured_tool_output(proc.stdout)
                        self.record_manual_fix(
                            filename,
                            "shfmt",
                            "Reformat the file with shfmt -i 4 -ci -sr -kp -w.",
                        )
            except Exception as e:
                logger.error(f"Error writing output to {filename}: {e}")
                result = False

        if result:
            logger.info("All shell script files passed formatting check.")

        return result

    def get_license_header(self, filename):
        """Get the license header for a file based on its extension."""
        for group, exts in self.file_utils.file_endings.items():
            if self.file_utils.is_file_in_group(filename, exts):
                return self.license_template_manager.get("cpp" if group == "web" else group)

        return None

    @staticmethod
    def stabilize_cmake_file(filename):
        """Re-run cmake-format after header insertion when the config is available."""
        config_file = ".cmake-format.yaml"  # NOSONAR: keep the repo-root config literal inline for cmake-format.
        if not os.path.isfile(config_file):
            logger.debug(
                f"Skipping post-header cmake-format for {filename}: {config_file} is unavailable."
            )
            return True

        proc = subprocess.run(
            ["cmake-format", "-c", config_file, "-i", filename],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            encoding="utf-8",
        )

        if proc.returncode == 0:
            return True

        logger.error(
            f"cmake-format failed to stabilize {filename} after adding a license header."
        )
        if proc.stdout:
            logger.error(proc.stdout)
        return False

    @staticmethod
    def existing_license_notices(content):
        """Recognize existing notices before changing or validating a file's licence."""
        lines = (line.strip(" #/*!\t") for line in content.splitlines())
        return [line for line in lines if re.match(
            r"SPDX-(?:FileCopyrightText|License-Identifier):", line, re.IGNORECASE)
            or re.search(r"\bcopyright\b", line, re.IGNORECASE)]

    def apply_license_header(self, filename, content):
        """Apply license header to a file, preserving shebang if present."""
        if self.existing_license_notices(content):
            self.record_manual_fix(
                filename, "license-header",
                "Existing copyright or licence notices require manual review; preserve their original terms.",
            )
            return False
        header = self.get_license_header(filename)
        if not header:
            logger.error(f"No license template for file: {filename}")
            return False

        lines = content.splitlines(keepends=True)
        new_content = ""
        if lines and lines[0].startswith("#!"):
            # Preserve shebang as first line
            new_content = lines[0] + header + '\n' + ''.join(lines[1:])
        else:
            separator = '\n'
            if self.file_utils.is_file_in_group(filename, self.file_utils.file_endings["cmake"]):
                # Keep CMake headers formatter-stable so a second run does not rewrite spacing.
                separator = ''
            new_content = header + separator + content

        try:
            with open(filename, 'w', encoding='utf-8', newline="\n") as f:
                f.write(new_content)
        except Exception as e:
            logger.error(f"Error writing license header to {filename}: {e}")
            return False

        if self.file_utils.is_file_in_group(filename, self.file_utils.file_endings["cmake"]):
            if not self.stabilize_cmake_file(filename):
                return False

        self.record_autofix(filename, "license-header", "added a missing header to")
        return True

    def check_license_header(self, files, format=True) -> bool:
        """Check license header in each file."""
        logger.info("Checking license headers in files...")

        result = True
        files = self.file_utils.filter_by_path_ending(
            files, self.file_utils.file_endings["license"])
        # Vendored browser assets retain their upstream notices and licence terms.
        project_root = self.file_utils.get_project_root()
        files = [filename for filename in files if not self.file_utils.is_ignored_file(
            os.path.relpath(filename, project_root), ["development/web/content/vendor"])]
        copyright_pattern = re.compile(
            r"SPDX-FileCopyrightText: Copyright \d{4}(?:-\d{4})?"
            r"(?:, \d{4}(?:-\d{4})?)* Arm Limited and/or its affiliates <perception-fdbck@arm\.com>")

        for filename in files:
            content = ""
            if not self.file_utils.is_file_in_group(filename, self.file_utils.file_endings["license"]):
                continue
            try:
                with open(filename, "r", encoding="utf-8", errors="ignore") as f:
                    content = f.read()
            except Exception as e:
                logger.error(f"Could not read file {filename}: {e}")
                result = False
                continue

            top_lines = content.splitlines()[:5]
            identifiers = [line.partition(":")[2].strip() for line in self.existing_license_notices(content)
                           if line.lower().startswith("spdx-license-identifier:")]
            template = self.get_license_header(filename)
            if not template or "Licensed under the Apache License" not in template:
                logger.error(f"Combined Apache licence template is missing for {filename}")
                result = False
                continue
            notice = "\n".join(line.strip(" #/*\t") for line in template.splitlines())
            notice = notice[notice.index("Licensed under the Apache License"):].strip()
            header = "\n".join(line.strip(" #/*\t") for line in content.splitlines()[:25])
            found = (
                any(copyright_pattern.search(line) for line in top_lines)
                and any(line.strip(" #/*\t") == "SPDX-License-Identifier: Apache-2.0" for line in top_lines)
                and set(identifiers) == {"Apache-2.0"}
                and notice in header
            )

            if found:
                logger.debug(f"License header already present in {filename}")
            elif format:
                result = False
                self.apply_license_header(filename, content)
            else:
                self.record_manual_fix(
                    filename,
                    "license-header",
                    "Use the combined SPDX and Apache short notice from tools/templates/header; "
                    "preserve existing copyright holders, contribution years, and licence terms.",
                )
                result = False

        if result:
            logger.info("License header check succeeded for all files.")

        return result
