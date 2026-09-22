################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

import os
import logging
import itertools
from git import Repo, GitCommandError

logger = logging.getLogger("opk_ci")


class FileUtils:
    """Utility class for file operations."""

    def __init__(self):
        """Initialize the FileUtils class."""
        self.file_endings = {
            "cpp": [".c", ".cpp", ".h", ".hpp", ".tpp", "*.cc", "*.cxx", "*.hxx", "*.hh"],
            "py": [".py", ".pyi", ".ipynb"],
            "cmake": [".cmake", "CMakeLists.txt"],
            "sh": [".sh"]
        }
        self.file_endings["license"] = list(itertools.chain.from_iterable(self.file_endings.values()))

    @staticmethod
    def get_project_root():
        """Get the root directory of the project."""
        return Repo(os.getcwd(), search_parent_directories=True).working_tree_dir

    @staticmethod
    def is_file_in_group(file_path, suffixes):
        for s in suffixes:
            if file_path.endswith(s):
                return True
        return False

    @staticmethod
    def filter_by_path_ending(files, exts):
        """Filter files by extensions and exact filenames (e.g., CMakeLists.txt)."""
        filtered = []

        for f in files:
            fname = os.path.basename(f)
            if FileUtils.is_file_in_group(fname, exts):
                filtered.append(f)

        return filtered

    @staticmethod
    def has_remote_branch_ref(repo, pr_target_branch):
        remote_ref = f"refs/remotes/origin/{pr_target_branch}"
        try:
            repo.git.rev_parse("--verify", remote_ref)
            return True
        except GitCommandError:
            return False

    @staticmethod
    def discover_git_files(repo, commit_diff=False, pr_target_branch=None):
        if pr_target_branch:
            try:
                repo.git.fetch("origin", pr_target_branch)
            except GitCommandError as exc:
                if not FileUtils.has_remote_branch_ref(repo, pr_target_branch):
                    raise
                logger.warning(
                    "Could not refresh origin/%s, using the existing local ref instead: %s",
                    pr_target_branch,
                    exc,
                )
            return repo.git.diff(f"origin/{pr_target_branch}...HEAD", name_only=True).splitlines()

        if commit_diff:
            return [item.a_path for item in repo.index.diff("HEAD")]

        return repo.git.ls_files().splitlines()

    @staticmethod
    def normalize_repo_path(file_path):
        normalized = file_path.replace(os.sep, "/").rstrip("/")
        while normalized.startswith("./"):
            normalized = normalized[2:]
        return normalized

    @staticmethod
    def is_ignored_file(file_path, ignore_folder):
        normalized = FileUtils.normalize_repo_path(file_path)
        return any(
            normalized == skip or normalized.startswith(f"{skip}/")
            for skip in (FileUtils.normalize_repo_path(folder) for folder in ignore_folder)
            if skip
        )

    @staticmethod
    def filter_existing_files(files, ignore_folder):
        return [
            f for f in files
            if os.path.exists(f) and (
                f.endswith(".git/COMMIT_EDITMSG")
                or not FileUtils.is_ignored_file(f, ignore_folder)
            )
        ]

    @staticmethod
    def get_related_files(commit_diff=False, pr_target_branch=None, files=None, ignore_folder=None):
        """Get files to check based on the current git state or all files in a folder."""
        logger.info("Searching for files to check...")

        files = [] if files is None else files
        ignore_folder = [] if ignore_folder is None else ignore_folder

        if not files:
            try:
                repo = Repo(os.getcwd(), search_parent_directories=True)
                files = FileUtils.discover_git_files(repo, commit_diff, pr_target_branch)
            except GitCommandError as e:
                logger.error(f"Could not get git files: {e}")
                raise
            except Exception as e:
                logger.error(f"Unexpected error getting git files: {e}")
                raise

        existing_files = FileUtils.filter_existing_files(files, ignore_folder)
        logger.info(f"Number of files to check: {len(existing_files)}")

        return existing_files
