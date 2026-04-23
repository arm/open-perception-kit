################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

import os
import re
import sys
import logging
import subprocess
import requests
from git import Repo, GitCommandError

from expkits_ci.license_template_manager import LicenseTemplateManager
from expkits_ci.file_utils import FileUtils

logger = logging.getLogger("expkits_ci")


class QualityChecks:
    """Class to perform various quality checks on files."""

    # Constants
    JIRA_PROJECTS = ["EXPKITS"]

    def __init__(self):
        self.license_template_manager = LicenseTemplateManager()
        self.file_utils = FileUtils()

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

        jira_pattern = r"^feature/(%s)-\d+/.+" % "|".join(
            QualityChecks.JIRA_PROJECTS)
        result = False
        # main branch -> should not be used for development
        # feature branch: feature/PROJECT-1234/something-something
        if re.match(jira_pattern, branch) or (branch == "main"):
            result = True
        # sandbox branch: sandbox/whatever
        elif branch.startswith("sandbox/"):
            result = True

        if not result:
            logger.error(f"Invalid branch name: \"{branch}\"")
            logger.info("Valid formats:")
            for proj in QualityChecks.JIRA_PROJECTS:
                logger.info(f"  feature/{proj}-1234/ticket-description")
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
            logger.error(
                "Commit message must have at least two lines: a description and a reference to a JIRA ticket.")
            logger.info("Example:")
            logger.info("  Add new feature for X\n  Task: EXPKITS-1234")
            logger.info(
                "Please rename your commit accordingly. Hint: git commit --amend")
            return False

        if not filtered_lines[0]:
            logger.error(
                "First line of commit message must be a non-empty description.")
            return False

        # Second line: <bug|task>: JIRA-XXXX
        jira_pattern = r"^(Bug|Task): (%s)-\d+$" % "|".join(QualityChecks.JIRA_PROJECTS)
        if not re.match(jira_pattern, filtered_lines[1], re.IGNORECASE):
            logger.error(
                f"Second line must match \"<Bug|Task>: JIRA-XXXX\" with a valid JIRA project.")
            logger.info("Example:")
            logger.info(f"  Task: {QualityChecks.JIRA_PROJECTS[0]}-1234")
            return False

        logger.info("Commit message format is valid.")
        return True

    @staticmethod
    def check_commit_messages_on_ci(files=None, target_branch=None) -> bool:
        """Check all commit messages on the current branch that are not on target_branch.

        When target_branch is provided the set of commits checked is
        those reachable from HEAD but not from the merge-base with target_branch,
        i.e. exactly the commits introduced by the current branch/PR.
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

                merge_base_list = repo.merge_base(repo.head.commit, target_commit)
                if not merge_base_list:
                    logger.error(f"Could not find merge base between HEAD and '{target_branch}'.")
                    return False
                merge_base = merge_base_list[0]
                logger.info(f"Checking commits between merge base {merge_base.hexsha[:8]} and HEAD")
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
        result = True

        for commit in commits:
            sha = commit.hexsha[:8]
            filtered_lines = QualityChecks.filter_comment_lines(commit.message)

            if len(filtered_lines) < 2:
                logger.error(
                    f"[{sha}] Commit message must have at least two lines: "
                    "a description and a reference to a JIRA ticket.")
                logger.error("Example:")
                logger.error("  Add new feature for X\n  Task: EXPKITS-1234")
                logger.error(
                    "The current commit message is:\n" + "\n".join(filtered_lines))
                result = False
                continue

            if not filtered_lines[0]:
                logger.error(f"[{sha}] First line of commit message must be a non-empty description.")
                logger.error(
                    "The current commit message is:\n" + "\n".join(filtered_lines))
                result = False
                continue

            if not re.match(jira_pattern, filtered_lines[1], re.IGNORECASE):
                logger.error(
                    f"[{sha}] Second line must match \"<Bug|Task>: JIRA-XXXX\" with a valid JIRA project.")
                logger.error("Example:")
                logger.error(f"  Task: {QualityChecks.JIRA_PROJECTS[0]}-1234")
                logger.error(
                    "The current commit message is:\n" + "\n".join(filtered_lines))
                result = False
                continue

            logger.info(f"[{sha}] Commit message format is valid.")

        return result

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
        if not files:
            # No files specified, check all git-tracked files
            try:
                result = subprocess.run(["git", "ls-files"], capture_output=True, text=True, check=True)
                files = result.stdout.strip().splitlines()
            except Exception as e:
                logger.error(f"Failed to get git-tracked files: {e}")
                result = False
                return result
        for file in files:
            if os.path.isfile(file):
                try:
                    subprocess.run(["detect-secrets-hook", "--baseline", baseline, file], check=True)
                except subprocess.CalledProcessError:
                    logger.error(f"Secrets detected in {file}")
                    result = False
        if result:
            logger.info("No secrets detected.")
        return result

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
                    cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

                if proc.returncode != 0:
                    logger.error(f"clang-format check failed for {filename}: \n{proc.stdout.decode(encoding='utf-8')}")
                    result = False
                    if format:
                        format_cmd = ["clang-format", "-i", filename]
                        subprocess.run(format_cmd)
                        logger.info(
                            f"Formatted {filename} using clang-format. Please check the file again.")
            except Exception as e:
                result = False
                logger.error(f"Error writing output to {filename}: {e}")

        if result:
            logger.info("All files passed clang-format check.")

        return result

    def remove_flags_from_file(self, file_path, flags):
        """Remove specific flag words from a file by simple text replacement."""
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
            for flag in flags:
                content = content.replace(flag, '')
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write(content)
            logger.info(f"Removed flags from {file_path}")
        except Exception as e:
            logger.error(f"Error removing flags from {file_path}: {e}")

    def check_clang_tidy(self, files) -> bool:
        """Check clang-tidy validity to files under folder."""
        logger.info("Checking clang-tidy validity...")

        files = self.file_utils.filter_by_path_ending(
            files, self.file_utils.file_endings["cpp"])
        result = True

        if not files:
            logger.info("No C/C++ files found to check with clang-tidy.")
            return result

        # TODO: known issue also described here:
        # https://github.com/llvm/llvm-project/pull/111453
        # For future use other zephyr supported static code analysis should be used
        # https://docs.zephyrproject.org/latest/develop/sca/index.html

        flags_to_filter = [
            "-fno-reorder-functions",
            "-mfp16-format=ieee",
            "-fno-defer-pop"
        ]
        compile_config_path = os.path.join(self.file_utils.get_project_root(), "workspace", "build")
        compile_commands_path = os.path.join(compile_config_path, "compile_commands.json")
        self.remove_flags_from_file(compile_commands_path, flags_to_filter)

        for f in files:
            try:
                # Run clang-tidy on each file
                cmd = ["clang-tidy", f, "-p", compile_config_path]

                proc = subprocess.run(
                    cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding="utf-8")

                if proc.returncode != 0:
                    logger.error(f"clang-tidy check failed for {f}.")
                    logger.error(proc.stdout)
                    logger.error(proc.stderr)
                    result = False
                elif proc.stdout:
                    logger.debug(f"clang-tidy output for {f}:\n{proc.stdout}")
            except subprocess.CalledProcessError as e:
                logger.error(f"clang-tidy failed for {f}: {e}")
                result = False
            except Exception as e:
                logger.error(f"Unknown error running clang-tidy on {f}: {e}")
                result = False

        if result:
            logger.info("All files passed clang-tidy check.")

        return result

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
                            logger.error(f"Formatted {f} using autopep8. Please check the file again.")
                            result = False
                        else:
                            logger.error(f"autopep8 failed to format {f}.")
                            logger.info(proc.stdout)
                            logger.info(proc.stderr)
                            result = False
                    else:
                        logger.error(f"PEP-8 check failed for {f}. For details run with --verbose.")
                        logger.info(proc.stdout)
                        result = False
                elif proc.returncode != 0:
                    logger.error(f"autopep8 check failed for {f}.")
                    logger.info(proc.stdout)
                    logger.info(proc.stderr)
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
            cmd = ["cmake-format", "-c", ".cmake-format.yaml", "--check", filename]

            try:
                proc = subprocess.run(
                    cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

                if proc.returncode != 0:
                    # If not formatting, show the diff
                    logger.error(f"cmake-format check failed for {filename}.")
                    result = False
                    if format:
                        format_cmd = ["cmake-format", "-c", ".cmake-format.yaml", "-i", filename]
                        subprocess.run(format_cmd)
                        logger.info(
                            f"Formatted {filename} using cmake-format.")
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
                    cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

                if proc.returncode != 0:
                    # If not formatting, show the diff
                    logger.error(f"shfmt check failed for {filename}.")
                    result = False
                    if format:
                        format_cmd = ["shfmt", *shfmt_args, "-w", filename]
                        subprocess.run(format_cmd)
                        logger.info(
                            f"Formatted {filename} using shfmt.")
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
                return self.license_template_manager.get(group)

        return None

    def apply_license_header(self, filename, content):
        """Apply license header to a file, preserving shebang if present."""
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
            new_content = header + '\n' + content

        try:
            with open(filename, 'w', encoding='utf-8', newline="\n") as f:
                f.write(new_content)
                logger.info(f"License header added to {filename}")
        except Exception as e:
            logger.error(f"Error writing license header to {filename}: {e}")
            return False

        return True

    def check_license_header(self, files, format=True) -> bool:
        """Check license header in each file."""
        logger.info("Checking license headers in files...")

        result = True
        files = self.file_utils.filter_by_path_ending(
            files, self.file_utils.file_endings["license"])
        copyright_pattern = re.compile(
            r"Copyright \(C\) \d{4} Arm Limited\. All rights reserved\.")

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
            found = any(copyright_pattern.search(line) for line in top_lines)

            if found:
                logger.debug(f"License header already present in {filename}")
            elif format:
                result = False
                self.apply_license_header(filename, content)
            else:
                logger.error(f"License header missing in {filename}.")
                logger.info(
                    "Please add the license header to the file or use --license-header to add it automatically.")
                result = False

        if result:
            logger.info("License header check succeeded for all files.")

        return result
