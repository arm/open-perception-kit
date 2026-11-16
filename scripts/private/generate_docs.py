#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

"""Prepare OPK documentation in a build copy and invoke the shared Arm renderer."""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import unquote, urlsplit

from inject_docs_build_info import format_build_info, replace_build_info_block

SOURCE = Path("/source")
OUTPUT = Path("/output")
SITE_ID = "open-perception-kit"
REPOSITORY_URL = "https://github.com/arm/open-perception-kit"
PLANTUML_JAR = "/opt/opk-deps/plantuml-mit-1.2026.2.jar"
LINK = re.compile(r"(!?\[[^\]\n]*\]\()([^\s)]+)(\))")
FENCE = re.compile(r"^(?P<fence>`{3,}|~{3,})[^\n]*\n.*?^(?P=fence)[ \t]*$", re.MULTILINE | re.DOTALL)
PLANTUML = re.compile(r"^```(?:plantuml|puml)\s*\n(.*?)\n```\s*$", re.DOTALL)


def run(*command: str, cwd: Path) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def prepare_site(work: Path, publishing: bool) -> tuple[Path, dict[Path, Path]]:
    site = work / "docs/site"
    shutil.copytree(SOURCE / "docs/public", site)
    pages = {path: site / path.relative_to(SOURCE / "docs/public")
             for path in (SOURCE / "docs/public").rglob("*.md")}
    if not publishing:
        shutil.copytree(SOURCE / "docs/arch", site / "architecture")
        pages.update({path: site / "architecture" / path.relative_to(SOURCE / "docs/arch")
                      for path in (SOURCE / "docs/arch").rglob("*.md")})
        for name in ("README.md", "third-party-licenses.md"):
            source = SOURCE / "docs" / name
            destination = site / ("documentation.md" if name == "README.md" else name)
            shutil.copy2(source, destination)
            pages[source] = destination
        config_path = site / "docs-config.json"
        config = json.loads(config_path.read_text())
        config["navbarItems"].append({"label": "Architecture", "to": "/architecture", "position": "left"})
        config["sidebarIDs"].append("architecture")
        config["sidebarDirectoryMap"]["architecture"] = ["architecture"]
        config_path.write_text(json.dumps(config, indent=2) + "\n")
    return site, pages


def render_diagram(content: str, source: Path, destination: Path, site: Path, index: int) -> str:
    # Resolve includes exactly as the Markdown preview does, relative to its page.
    diagram = site / "assets/diagrams" / destination.relative_to(site).with_suffix("") / f"{index}.svg"
    diagram.parent.mkdir(parents=True, exist_ok=True)
    rendered = subprocess.run(
        ["java", "-Djava.awt.headless=true", f"-Dplantuml.include.path={source.parent}",
         "-jar", PLANTUML_JAR, "-pipe", "-tsvg", "-charset", "UTF-8", "-failfast2"],
        input=content, text=True, capture_output=True, check=True,
    )
    if "<svg" not in rendered.stdout or "Syntax Error?" in rendered.stdout:
        raise ValueError(f"PlantUML did not render {source}")
    diagram.write_text(rendered.stdout)
    relative = os.path.relpath(diagram, destination.parent)
    return f"![{destination.stem.replace('-', ' ')} diagram]({relative})"


def rewrite_link(match: re.Match[str], source: Path, destination: Path,
                 pages: dict[Path, Path], site: Path, publishing: bool) -> str:
    link = urlsplit(match[2])
    if link.scheme or link.netloc or not link.path or link.path.startswith("/"):
        return match[0]
    target = (source.parent / unquote(link.path)).resolve()
    if not target.is_relative_to(SOURCE):
        raise ValueError(f"Link escapes the checkout: {source}: {match[2]}")
    if not target.exists():
        raise ValueError(f"Missing link target: {source}: {match[2]}")
    if target in pages:
        url = os.path.relpath(pages[target], destination.parent)
    elif target.is_relative_to(SOURCE / "docs/public"):
        url = os.path.relpath(site / target.relative_to(SOURCE / "docs/public"), destination.parent)
    else:
        # Repository references remain source links, outside the documentation site.
        revision = os.environ["OPK_DOCS_COMMIT"] if publishing else "main"
        kind = "tree" if target.is_dir() else "blob"
        url = f"{REPOSITORY_URL}/{kind}/{revision}/{target.relative_to(SOURCE)}"
    if link.query:
        url += "?" + link.query
    if link.fragment:
        url += "#" + link.fragment
    return match[1] + url + match[3]


def prepare_markdown(site: Path, pages: dict[Path, Path], publishing: bool) -> None:
    for source, destination in pages.items():
        text = source.read_text()
        parts = []
        previous = 0
        for fence in FENCE.finditer(text):
            parts.extend((text[previous:fence.start()], fence[0]))
            previous = fence.end()
        parts.append(text[previous:])
        for index, part in enumerate(parts):
            diagram = PLANTUML.fullmatch(part)
            if diagram:
                parts[index] = render_diagram(diagram[1], source, destination, site, index)
            elif index % 2 == 0:
                parts[index] = LINK.sub(
                    lambda match: rewrite_link(match, source, destination, pages, site, publishing), part)
        destination.write_text("".join(parts))


def generate_api(site: Path, work: Path) -> None:
    api_output = site / "static/api"
    api_output.mkdir(parents=True)
    config = (SOURCE / "development/Doxyfile").read_text()
    # Keep portable filenames and emit referenced files even without a file comment.
    config += (f"\nOUTPUT_DIRECTORY = {api_output}\nINPUT = {SOURCE / 'development'}\n"
               "GENERATE_LATEX = NO\nCASE_SENSE_NAMES = NO\nEXTRACT_ALL = YES\nEXCLUDE_PATTERNS += *.md\n")
    config_path = work / "Doxyfile"
    config_path.write_text(config)
    run("doxygen", str(config_path), cwd=SOURCE / "development")
    (site / "api.md").write_text(
        '# API reference\n\n<a href="html/index.html">Open the C++ API reference</a>.\n')
    config_path = site / "docs-config.json"
    config = json.loads(config_path.read_text())
    config["navbarItems"].append({"label": "API reference", "to": "/api", "position": "left"})
    config_path.write_text(json.dumps(config, indent=2) + "\n")


class HtmlLinks(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.links: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        for name, value in attrs:
            if value and (name == "src" or (name == "href" and tag in ("a", "link"))):
                self.links.append(value)


def validate_assets(built: Path, site_url: str) -> None:
    prefix = urlsplit(site_url).path.rstrip("/") + "/" + SITE_ID + "/"
    for page in built.rglob("*.html"):
        parser = HtmlLinks()
        parser.feed(page.read_text())
        for value in parser.links:
            link = urlsplit(value)
            if link.scheme or link.netloc or not link.path:
                continue
            path = unquote(link.path)
            if path.startswith(prefix):
                target = built / path.removeprefix(prefix)
            elif path.startswith("/"):
                raise ValueError(f"Link outside the generated site: {page}: {value}")
            else:
                target = page.parent / path
            target = target.resolve()
            if not target.is_relative_to(built) or not target.exists():
                raise ValueError(f"Missing generated asset or link: {page}: {value}")
            if target.is_dir() and not (target / "index.html").is_file():
                raise ValueError(f"Missing generated page index: {page}: {value}")


def write_output(directory: Path) -> None:
    # Preserve the last successful output if preparation or rendering failed.
    for child in OUTPUT.iterdir():
        if child.is_dir() and not child.is_symlink():
            shutil.rmtree(child)
        else:
            child.unlink()
    shutil.copytree(directory, OUTPUT, dirs_exist_ok=True)


def build(args: argparse.Namespace) -> None:
    with tempfile.TemporaryDirectory(prefix="opk-docs-") as temporary:
        work = Path(temporary)
        site, pages = prepare_site(work, args.prepare_publish)
        prepare_markdown(site, pages, args.prepare_publish)
        static = site / "static"
        static.mkdir(exist_ok=True)
        # Record provenance and keep the released renderer's static input nonempty.
        (static / "source-revision.txt").write_text(os.environ["OPK_DOCS_COMMIT"] + "\n")
        if args.with_api:
            generate_api(site, work)
        if args.prepare_publish:
            index = site / "index.md"
            metadata = format_build_info(os.environ["OPK_DOCS_COMMIT"])
            index.write_text(replace_build_info_block(index.read_text(), metadata))
            write_output(site)
            print("Prepared docs/prepared-public for the existing docs-action validate/publish actions.")
            return
        run("git", "init", "--quiet", cwd=work)
        run("git", "remote", "add", "origin", REPOSITORY_URL, cwd=work)
        renderer_temp = work / "renderer"
        renderer_temp.mkdir()
        subprocess.run(
            ["arm-docs", "validate", "--docs-root", "docs/site", "--site-id", SITE_ID],
            cwd=work, env={**os.environ, "TMPDIR": str(renderer_temp)}, check=True,
        )
        # arm-docs 0.12.1 retains its validated output in TMPDIR without an output flag.
        builds = list(renderer_temp.glob(f"arm-docs-build-{SITE_ID}-*"))
        if len(builds) != 1 or not (builds[0] / "index.html").is_file():
            raise ValueError("The released renderer did not produce one documentation site")
        validate_assets(builds[0], "http://localhost:3003")
        (work / "html").mkdir()
        shutil.move(str(builds[0]), work / "html" / SITE_ID)
        (work / "html/index.html").write_text(
            f'<!doctype html><meta http-equiv="refresh" content="0;url={SITE_ID}/">'
            f'<a href="{SITE_ID}/">Open Perception Kit documentation</a>\n')
        write_output(work / "html")
        print("Generated docs/html. Run ./scripts/serve-docs.sh to view it.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--with-api", action="store_true")
    mode.add_argument("--prepare-publish", action="store_true")
    args = parser.parse_args()
    build(args)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Documentation generation failed: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr, file=sys.stderr)
        raise SystemExit(1) from error
