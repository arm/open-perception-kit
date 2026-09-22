#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import os
import sys
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "plumber"))

from plumber.plumber import (  # noqa: E402
    build_arg_parser,
    main,
    resolve_project_root,
    start_pipeline,
)


class PlumberProjectRootTests(unittest.TestCase):
    def test_defaults_to_container_project_root(self) -> None:
        with mock.patch.dict(os.environ, {}, clear=True):
            project_root = resolve_project_root()
            args = build_arg_parser(project_root).parse_args(
                ["pipeline", "save", "ground-truth.ndjson"]
            )

        self.assertEqual(project_root, Path("/work"))
        self.assertEqual(args.project_root, Path("/work"))
        self.assertEqual(args.opk_menu, "/work/tools/opk-menu")
        self.assertIsNone(args.fifo)

    @mock.patch("plumber.plumber.run_save_mode")
    def test_main_uses_private_fifo_by_default(self, run_save_mode: mock.Mock) -> None:
        def check_fifo(args) -> int:
            fifo = Path(args.fifo)
            self.assertFalse(fifo.exists())
            self.assertEqual(fifo.parent.stat().st_mode & 0o777, 0o700)
            return 0

        run_save_mode.side_effect = check_fifo
        with mock.patch.object(
            sys,
            "argv",
            ["plumber", "pipeline", "save", "ground-truth.ndjson"],
        ):
            self.assertEqual(main(), 0)

        run_save_mode.assert_called_once()

    def test_uses_environment_project_root_for_defaults(self) -> None:
        checkout = Path("/home/developer/amp-dev-forge")
        with mock.patch.dict(
            os.environ,
            {"OPK_PROJECT_ROOT": str(checkout)},
            clear=True,
        ):
            project_root = resolve_project_root()
            args = build_arg_parser(project_root).parse_args(
                ["pipeline", "check", "ground-truth.ndjson"]
            )

        self.assertEqual(args.project_root, checkout)
        self.assertEqual(
            args.opk_menu,
            "/home/developer/amp-dev-forge/tools/opk-menu",
        )

    def test_rejects_relative_environment_project_root(self) -> None:
        with mock.patch.dict(
            os.environ,
            {"OPK_PROJECT_ROOT": "relative/checkout"},
            clear=True,
        ):
            with self.assertRaisesRegex(
                ValueError,
                "OPK_PROJECT_ROOT must be an absolute path",
            ):
                resolve_project_root()

    @mock.patch("plumber.plumber.subprocess.Popen")
    def test_pipeline_uses_project_root_as_cwd_and_environment(
        self,
        popen: mock.Mock,
    ) -> None:
        checkout = Path("/home/developer/amp-dev-forge")

        start_pipeline(
            str(checkout / "tools/opk-menu"),
            "pipeline",
            ["--example"],
            "/tmp/opkcomm",
            checkout,
        )

        popen.assert_called_once()
        command = popen.call_args.args[0]
        options = popen.call_args.kwargs
        self.assertEqual(
            command,
            [
                "/home/developer/amp-dev-forge/tools/opk-menu",
                "pipeline",
                "--example",
            ],
        )
        self.assertEqual(options["cwd"], checkout)
        self.assertEqual(options["env"]["OPK_PROJECT_ROOT"], str(checkout))
        self.assertEqual(options["env"]["OPKCOMM_FILE"], "/tmp/opkcomm")


if __name__ == "__main__":
    unittest.main()
