#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


PRIVATE_SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PRIVATE_SCRIPTS))
import initialize_models  # noqa: E402


class InitializeModelsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.workspace = Path(self.tempdir.name).resolve()
        (self.workspace / "config/models").mkdir(parents=True)
        self.modelfetch = self.workspace / "bin/modelfetch"
        self.modelfetch.parent.mkdir(parents=True)
        self.modelfetch.touch(mode=0o755)
        self.modelfetch.chmod(0o755)

    def write_json(self, relative_path: str, document: object) -> Path:
        path = self.workspace / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(document), encoding="utf-8")
        return path

    def test_discovers_recursive_descriptors_and_batches_unique_assets(self) -> None:
        first = "hf:Arm/a@0123456789abcdef0123456789abcdef01234567#file=a.onnx"
        second = "hf:Arm/b@0123456789abcdef0123456789abcdef01234567#file=b.onnx"
        self.write_json("config/models/z/model.json", {"modelFile": second})
        self.write_json("config/models/a/nonstandard-name.json", {"modelFile": first})
        self.write_json("config/models/duplicate/model.json", {"modelFile": second})
        self.write_json("config/models/local/model.json", {"modelFile": "model.onnx"})
        self.write_json("config/models/local/opchain.json", {"ops": []})
        captured_request: dict[str, object] = {}

        def successful_download(
            command: list[str], **_kwargs: object
        ) -> subprocess.CompletedProcess[str]:
            request_path = Path(command[-1])
            captured_request.update(
                json.loads(request_path.read_text(encoding="utf-8"))
            )
            return subprocess.CompletedProcess(command, 0, '{"results":[]}', "")

        with mock.patch.object(
            initialize_models.subprocess,
            "run",
            side_effect=successful_download,
        ) as run:
            initialize_models.initialize_models(
                workspace_root=self.workspace,
                modelfetch_executable=self.modelfetch,
            )

        self.assertEqual(
            run.call_args.args[0][:3],
            [str(self.modelfetch), "models", "download-request"],
        )
        self.assertFalse(Path(run.call_args.args[0][-1]).exists())
        self.assertEqual(
            captured_request,
            {
                "downloads": [
                    {
                        "asset_id": first,
                        "destination": str(self.workspace / "var/models"),
                    },
                    {
                        "asset_id": second,
                        "destination": str(self.workspace / "var/models"),
                    },
                ]
            },
        )

    def test_modelfetch_is_the_asset_id_validation_authority(self) -> None:
        self.write_json(
            "config/models/malformed/model.json",
            {"modelFile": "hf:not-canonical#file=model.onnx"},
        )
        failure = subprocess.CompletedProcess(
            [], 2, "", "Asset ID must be a canonical pinned locator"
        )

        with mock.patch.object(
            initialize_models.subprocess, "run", return_value=failure
        ):
            with self.assertRaisesRegex(
                initialize_models.ModelInitializationError,
                "canonical pinned locator",
            ):
                initialize_models.initialize_models(
                    workspace_root=self.workspace,
                    modelfetch_executable=self.modelfetch,
                )

    def test_published_descriptor_must_identify_a_file_asset(self) -> None:
        self.write_json(
            "config/models/bundle/model.json",
            {
                "modelFile": "hf:Arm/model@0123456789abcdef0123456789abcdef01234567#bundle"
            },
        )

        with self.assertRaisesRegex(
            initialize_models.ModelInitializationError,
            "must identify one file asset",
        ):
            initialize_models.collect_asset_ids(self.workspace)

    def test_invalid_descriptor_model_file_is_rejected(self) -> None:
        self.write_json("config/models/invalid/model.json", {"modelFile": 42})

        with self.assertRaisesRegex(
            initialize_models.ModelInitializationError, "invalid modelFile"
        ):
            initialize_models.collect_asset_ids(self.workspace)

    def test_model_config_root_must_stay_inside_workspace(self) -> None:
        outside_models = self.workspace.parent / f"{self.workspace.name}-models"
        outside_models.mkdir()
        self.addCleanup(outside_models.rmdir)
        (self.workspace / "config/models").rmdir()
        (self.workspace / "config/models").symlink_to(
            outside_models, target_is_directory=True
        )

        with self.assertRaisesRegex(
            initialize_models.ModelInitializationError, "missing or unsafe"
        ):
            initialize_models.collect_asset_ids(self.workspace)

    def test_model_config_file_must_stay_inside_config_root(self) -> None:
        outside_config = self.workspace.parent / f"{self.workspace.name}-model.json"
        outside_config.write_text('{"modelFile":"outside.onnx"}', encoding="utf-8")
        self.addCleanup(outside_config.unlink)
        (self.workspace / "config/models/escaped.json").symlink_to(outside_config)

        with self.assertRaisesRegex(
            initialize_models.ModelInitializationError, "escapes"
        ):
            initialize_models.collect_asset_ids(self.workspace)

    def test_model_store_must_stay_inside_workspace(self) -> None:
        outside_store = self.workspace.parent / f"{self.workspace.name}-store"
        outside_store.mkdir()
        self.addCleanup(outside_store.rmdir)
        (self.workspace / "var").mkdir()
        (self.workspace / "var/models").symlink_to(
            outside_store, target_is_directory=True
        )

        with self.assertRaisesRegex(
            initialize_models.ModelInitializationError,
            "Model store escapes workspace root",
        ):
            initialize_models.download_assets(
                ["hf:Arm/model@0123456789abcdef0123456789abcdef01234567#file=a.onnx"],
                executable=self.modelfetch,
                workspace_root=self.workspace,
            )


if __name__ == "__main__":
    unittest.main()
