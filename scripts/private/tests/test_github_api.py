################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from email.message import Message
import importlib
import io
from pathlib import Path
import stat
import sys
import tempfile
import urllib.error
import urllib.parse
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

github_api = importlib.import_module("github_api")


def http_headers(values: dict[str, str] | None = None) -> Message[str, str]:
    headers: Message[str, str] = Message()
    for key, value in (values or {}).items():
        headers[key] = value
    return headers


def build_zip_archive(files: dict[str, str]) -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        for path, content in files.items():
            archive.writestr(path, content)
    return buffer.getvalue()


def build_zip_archive_with_symlink(*, link_name: str, link_target: str, files: dict[str, str]) -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        link_info = zipfile.ZipInfo(link_name)
        link_info.create_system = 3
        link_info.external_attr = (stat.S_IFLNK | 0o777) << 16
        archive.writestr(link_info, link_target)
        for path, content in files.items():
            archive.writestr(path, content)
    return buffer.getvalue()


class GitHubApiTests(unittest.TestCase):
    def test_download_github_archive_follows_redirect_location(self):
        redirect_error = urllib.error.HTTPError(
            url="https://api.github.com/repos/arm/open-perception-kit/actions/artifacts/1/zip",
            code=302,
            msg="Found",
            hdrs=http_headers({"Location": "https://objects.githubusercontent.com/archive.zip"}),
            fp=None,
        )
        opener = mock.Mock()
        opener.open.side_effect = redirect_error
        redirect_response = mock.MagicMock(status=200, reason="OK")
        redirect_response.read.return_value = b"zip-bytes"
        redirect_connection = mock.MagicMock()
        redirect_connection.getresponse.return_value = redirect_response

        with mock.patch.dict("os.environ", {"GH_TOKEN": "test-token"}, clear=False):
            with mock.patch("urllib.request.build_opener", return_value=opener):
                with mock.patch("http.client.HTTPSConnection", return_value=redirect_connection) as connection:
                    result = github_api.download_github_archive(
                        "https://api.github.com/repos/arm/open-perception-kit/actions/artifacts/1/zip",
                    )

        self.assertEqual(result, b"zip-bytes")
        connection.assert_called_once_with("objects.githubusercontent.com", timeout=60)
        redirect_connection.request.assert_called_once_with(
            "GET",
            "/archive.zip",
            headers={"User-Agent": github_api.GITHUB_USER_AGENT},
        )
        redirect_connection.close.assert_called_once_with()

    def test_download_github_archive_rejects_unsafe_redirect_location(self):
        cleartext_location = urllib.parse.urlunsplit(
            ("http", "objects.githubusercontent.com", "/archive.zip", "", "")
        )
        for location in (
            cleartext_location,
            "https://example.com/archive.zip",
        ):
            with self.subTest(location=location):
                redirect_error = urllib.error.HTTPError(
                    url="https://api.github.com/repos/arm/open-perception-kit/actions/artifacts/1/zip",
                    code=302,
                    msg="Found",
                    hdrs=http_headers({"Location": location}),
                    fp=None,
                )
                opener = mock.Mock()
                opener.open.side_effect = redirect_error

                with mock.patch.dict("os.environ", {"GH_TOKEN": "test-token"}, clear=False):
                    with mock.patch("urllib.request.build_opener", return_value=opener):
                        with self.assertRaisesRegex(ValueError, "GitHub archive redirect URL"):
                            github_api.download_github_archive(
                                "https://api.github.com/repos/arm/open-perception-kit/actions/artifacts/1/zip",
                            )

    def test_github_api_json_requires_relative_endpoint(self):
        with self.assertRaisesRegex(ValueError, "must be relative"):
            github_api.github_api_json("https://api.github.com/user")

    def test_github_api_headers_distinguish_required_and_optional_auth(self):
        with mock.patch.dict("os.environ", {"GH_TOKEN": "env-token"}, clear=True):
            required_headers = github_api.github_api_headers(token="")
            optional_headers = github_api.github_api_headers(
                token=None,
                require_token=False,
            )

        self.assertEqual(required_headers["Authorization"], "Bearer env-token")
        self.assertNotIn("Authorization", optional_headers)

    def test_github_api_query_endpoint_encodes_parameters(self):
        endpoint = github_api.github_api_query_endpoint(
            "repos/arm/open-perception-kit/actions/workflows/agent-review.yml/runs",
            {"branch": "feature/with space&marker", "per_page": 20},
        )

        self.assertEqual(
            endpoint,
            "repos/arm/open-perception-kit/actions/workflows/agent-review.yml/runs"
            "?branch=feature%2Fwith+space%26marker&per_page=20",
        )

    def test_extract_archive_bytes_rejects_members_outside_destination(self):
        for member_template in ("../outside.txt", "{temp_root}/outside.txt"):
            with self.subTest(member_template=member_template):
                with tempfile.TemporaryDirectory() as temp_dir:
                    temp_root = Path(temp_dir)
                    destination = temp_root / "destination"
                    member_name = member_template.format(temp_root=temp_root)
                    archive = build_zip_archive(
                        {
                            "logs/job.txt": "hello from logs\n",
                            member_name: "owned\n",
                        }
                    )

                    with self.assertRaisesRegex(RuntimeError, "escapes destination"):
                        github_api.extract_archive_bytes(archive, destination)

                    self.assertFalse((temp_root / "outside.txt").exists())
                    self.assertFalse((destination / "logs/job.txt").exists())

    def test_extract_archive_bytes_rejects_symlink_members_before_writes(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            destination = temp_root / "destination"
            outside = temp_root / "outside.txt"
            archive = build_zip_archive_with_symlink(
                link_name="logs/link",
                link_target="../outside.txt",
                files={
                    "logs/link/owned.txt": "owned\n",
                    "logs/job.txt": "hello from logs\n",
                },
            )

            with self.assertRaisesRegex(RuntimeError, "Archive member is a symlink"):
                github_api.extract_archive_bytes(archive, destination)

            self.assertFalse(outside.exists())
            self.assertFalse((destination / "logs/job.txt").exists())

    def test_extract_archive_bytes_returns_extracted_files(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            destination = Path(temp_dir) / "destination"
            archive = build_zip_archive(
                {
                    "logs/job.txt": "hello from logs\n",
                    "artifact/report.md": "# report\n",
                }
            )

            extracted = github_api.extract_archive_bytes(archive, destination)

        self.assertEqual(
            [path.relative_to(destination).as_posix() for path in extracted],
            ["artifact/report.md", "logs/job.txt"],
        )

    def test_github_api_does_not_use_unsafe_zip_extractall(self):
        module_file = github_api.__file__
        self.assertIsNotNone(module_file)
        content = Path(str(module_file)).read_text(encoding="utf-8")

        self.assertNotIn(".extractall(", content)


if __name__ == "__main__":
    unittest.main()
