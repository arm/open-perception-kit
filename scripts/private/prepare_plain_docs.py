#!/usr/bin/env python3
################################################################
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
################################################################

from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

HEADING_PATTERN = re.compile(r"^#\s+(\S(?:.*\S)?)\s*$")
FRONT_MATTER_PATTERN = re.compile(
    r"\A---[ \t]*\r?\n.*?\r?\n---[ \t]*(?:\r?\n|$)",
    re.DOTALL,
)


@dataclass(frozen=True)
class CategoryInfo:
    title: str
    description: str = ""


@dataclass(frozen=True)
class PlainDocsGenerator:
    source_dir: Path
    output_dir: Path
    clean: bool = False

    def strip_front_matter(self, text: str) -> str:
        return FRONT_MATTER_PATTERN.sub("", text, count=1)

    def default_title(self, path: Path) -> str:
        return path.stem.replace("-", " ").replace("_", " ").title()

    def first_heading(self, path: Path) -> str:
        if not path.exists():
            return self.default_title(path)

        text = self.strip_front_matter(path.read_text(encoding="utf-8"))
        for line in text.splitlines():
            match = HEADING_PATTERN.match(line)
            if match:
                return match.group(1)

        return self.default_title(path)

    def read_category_info(self, directory: Path) -> CategoryInfo:
        category_path = directory / "_category_.json"
        title = directory.name.replace("-", " ").replace("_", " ").title()
        description = ""

        if not category_path.exists():
            return CategoryInfo(title=title, description=description)

        try:
            category = json.loads(category_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            return CategoryInfo(title=title, description=description)

        title = category.get("label", title)
        link = category.get("link", {})
        if isinstance(link, dict) and link.get("type") == "generated-index":
            title = link.get("title", title)
            description = link.get("description", "")

        return CategoryInfo(title=title, description=description)

    def should_link_subdirectory(self, directory: Path) -> bool:
        return (directory / "index.md").exists() or (directory / "_category_.json").exists()

    def is_relative_to(self, path: Path, other: Path) -> bool:
        try:
            path.relative_to(other)
        except ValueError:
            return False
        return True

    def validate_source_dir(self) -> None:
        if not self.source_dir.exists():
            raise FileNotFoundError(f"Source directory does not exist: {self.source_dir}")
        if not self.source_dir.is_dir():
            raise NotADirectoryError(f"Source path is not a directory: {self.source_dir}")

    def validate_output_dir(self) -> None:
        if self.output_dir == self.source_dir:
            raise ValueError("Output directory must be different from the source directory.")
        if self.is_relative_to(self.output_dir, self.source_dir):
            raise ValueError("Output directory must not be inside the source directory.")
        if self.output_dir.exists() and not self.output_dir.is_dir():
            raise NotADirectoryError(f"Output path is not a directory: {self.output_dir}")

    def clear_directory(self, directory: Path) -> None:
        for child in directory.iterdir():
            if child.is_dir() and not child.is_symlink():
                shutil.rmtree(child)
            else:
                child.unlink()

    def prepare_output_dir(self) -> None:
        if self.output_dir.exists():
            if any(self.output_dir.iterdir()):
                if not self.clean:
                    raise ValueError(
                        f"Output directory is not empty: {self.output_dir}. Use --clean to "
                        "remove existing contents."
                    )
                self.clear_directory(self.output_dir)
            return

        self.output_dir.mkdir(parents=True, exist_ok=True)

    def copy_source_tree(self) -> None:
        for source_path in sorted(self.source_dir.rglob("*")):
            relative_path = source_path.relative_to(self.source_dir)
            destination_path = self.output_dir / relative_path

            if source_path.is_dir():
                destination_path.mkdir(parents=True, exist_ok=True)
                continue

            destination_path.parent.mkdir(parents=True, exist_ok=True)
            if source_path.suffix.lower() == ".md":
                destination_path.write_text(
                    self.strip_front_matter(source_path.read_text(encoding="utf-8")),
                    encoding="utf-8",
                )
            else:
                shutil.copy2(source_path, destination_path)

    def create_generated_indexes(self) -> None:
        directories = [
            self.source_dir,
            *sorted(path for path in self.source_dir.rglob("*") if path.is_dir()),
        ]
        for current_dir in directories:
            output_index = self.output_dir / current_dir.relative_to(self.source_dir) / "index.md"
            if (current_dir / "index.md").exists() or output_index.exists():
                continue

            markdown_children = sorted(
                path for path in current_dir.glob("*.md") if path.name != "index.md"
            )
            subdirectories = sorted(
                path
                for path in current_dir.iterdir()
                if path.is_dir() and not path.name.startswith(".")
            )
            linked_subdirectories = [
                path for path in subdirectories if self.should_link_subdirectory(path)
            ]

            if not markdown_children and not linked_subdirectories:
                continue

            category_info = self.read_category_info(current_dir)
            lines = [f"# {category_info.title}", ""]

            if category_info.description:
                lines.extend([category_info.description, ""])

            if markdown_children:
                lines.extend(["## Pages", ""])
                for child in markdown_children:
                    lines.append(f"- [{self.first_heading(child)}]({child.stem}.md)")
                lines.append("")

            if linked_subdirectories:
                lines.extend(["## Sections", ""])
                for child_dir in linked_subdirectories:
                    child_info = self.read_category_info(child_dir)
                    lines.append(f"- [{child_info.title}]({child_dir.name}/index.md)")
                lines.append("")

            output_index.parent.mkdir(parents=True, exist_ok=True)
            output_index.write_text("\n".join(lines).strip() + "\n", encoding="utf-8")

    def generate(self) -> None:
        self.validate_source_dir()
        self.validate_output_dir()
        self.prepare_output_dir()
        self.copy_source_tree()
        self.create_generated_indexes()


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Prepare Docusaurus-flavored Markdown for plain HTML generation.",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Remove existing output contents before generating files.",
    )
    parser.add_argument("source_dir", type=Path)
    parser.add_argument("output_dir", type=Path)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    generator = PlainDocsGenerator(
        source_dir=args.source_dir.resolve(),
        output_dir=args.output_dir.resolve(),
        clean=args.clean,
    )
    generator.generate()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, NotADirectoryError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        raise SystemExit(2)
