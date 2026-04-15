################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

import os
import logging
import itertools
from git import Repo, GitCommandError

logger = logging.getLogger("expkits_ci")


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
    def get_related_files(commit_diff=False, pr_target_branch=None, files=[], ignore_folder=[]):
        """Get files to check based on the current git state or all files in a folder."""
        logger.info("Searching for files to check...")

        if not files:
            try:
                repo = Repo(os.getcwd(), search_parent_directories=True)
                if pr_target_branch:
                    try:
                        repo.git.fetch("origin", pr_target_branch)
                    except GitCommandError as e:
                        logger.error(f"Could not fetch branch {pr_target_branch}: {e}")

                    files = repo.git.diff(f"origin/{pr_target_branch}...HEAD", name_only=True).splitlines()
                elif commit_diff:
                    files = [item.a_path for item in repo.index.diff("HEAD")]
                else:
                    files = repo.git.ls_files().splitlines()
            except GitCommandError as e:
                logger.error(f"Could not get git files: {e}")
                raise
            except Exception as e:
                logger.error(f"Unexpected error getting git files: {e}")
                raise

        existing_files = [
            f for f in files
            if os.path.exists(f) and (
                f.endswith(".git/COMMIT_EDITMSG")
                or not any(f.startswith(skip) for skip in ignore_folder)
            )
        ]
        logger.info(f"Number of files to check: {len(existing_files)}")

        return existing_files
