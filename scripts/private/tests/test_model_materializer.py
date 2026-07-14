#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from collections import Counter
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


PRIVATE_SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PRIVATE_SCRIPTS))
import model_materializer  # noqa: E402


class ModelMaterializerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.workspace = Path(self.tempdir.name).resolve()
        (self.workspace / "config/models").mkdir(parents=True)
        self.modelfetch = self.workspace / "bin/modelfetch"
        self.modelfetch.parent.mkdir(parents=True)
        self.modelfetch.touch(mode=0o755)
        self.modelfetch.chmod(0o755)
        self.requests: list[dict[str, object]] = []

    def write_json(self, relative_path: str, document: object) -> Path:
        path = self.workspace / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(document), encoding="utf-8")
        return path

    def write_descriptor(self, name: str, model_file: str) -> Path:
        return self.write_json(
            f"config/models/{name}/model.json",
            {"name": name, "modelFile": model_file},
        )

    def write_local_model(self, name: str, model_file: str) -> Path:
        path = self.workspace / f"config/models/{name}/{model_file}"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"model")
        return path

    def write_opchain(self, name: str, descriptors: list[Path]) -> Path:
        operations = [
            {
                "id": "inference",
                "attributes": {
                    "modelDescriptor": f"/work/{descriptor.relative_to(self.workspace)}"
                },
            }
            for descriptor in descriptors
        ]
        return self.write_json(
            f"config/opchains/{name}/opchain.json", {"ops": operations}
        )

    def successful_download(
        self, command: list[str], **_kwargs: object
    ) -> subprocess.CompletedProcess[str]:
        request_path = Path(command[-1])
        request = json.loads(request_path.read_text(encoding="utf-8"))
        self.requests.append(request)
        results = []
        for item in request["downloads"]:
            results.append(
                {
                    "asset_id": item["asset_id"],
                    "status": "downloaded",
                    "paths": ["unused-by-amp"],
                    "integrity": [f"sha256:{'b' * 64}"],
                }
            )
        return subprocess.CompletedProcess(
            command, 0, json.dumps({"results": results}), ""
        )

    def materialize(self, opchains: list[Path]) -> Counter[str]:
        return model_materializer.materialize_opchains(
            opchains,
            workspace_root=self.workspace,
            modelfetch_executable=self.modelfetch,
        )

    def test_batches_unique_published_assets_into_the_shared_store(self) -> None:
        asset_id = (
            "hf:Arm/example@0123456789abcdef0123456789abcdef01234567"
            "#file=onnx/model.onnx"
        )
        remote = self.write_descriptor("remote", asset_id)
        self.write_local_model("local", "local-model.onnx")
        local = self.write_descriptor("local", "local-model.onnx")
        first = self.write_opchain("first", [remote, local])
        duplicate = self.write_opchain("duplicate", [remote])

        with mock.patch.object(
            model_materializer.subprocess,
            "run",
            side_effect=self.successful_download,
        ) as run:
            statuses = self.materialize([first, duplicate])

        self.assertEqual(statuses["downloaded"], 1)
        command = run.call_args.args[0]
        self.assertEqual(
            command[:3], [str(self.modelfetch), "models", "download-request"]
        )
        self.assertFalse(
            Path(command[-1]).exists(), "temporary request must be removed"
        )
        self.assertEqual(
            self.requests,
            [
                {
                    "downloads": [
                        {
                            "asset_id": asset_id,
                            "destination": str(self.workspace / "var/models"),
                        }
                    ]
                }
            ],
        )

    def test_local_only_opchain_does_not_invoke_modelfetch(self) -> None:
        self.write_local_model("local", "local-model.onnx")
        local = self.write_descriptor("local", "local-model.onnx")
        opchain = self.write_opchain("local", [local])

        with mock.patch.object(model_materializer.subprocess, "run") as run:
            statuses = self.materialize([opchain])

        self.assertFalse(statuses)
        run.assert_not_called()

    def test_missing_local_model_fails_before_pipeline_start(self) -> None:
        local = self.write_descriptor("local", "missing-local-model.onnx")
        opchain = self.write_opchain("local", [local])

        with mock.patch.object(model_materializer.subprocess, "run") as run:
            with self.assertRaisesRegex(
                model_materializer.MaterializationError,
                "Local model file does not exist",
            ):
                self.materialize([opchain])

        run.assert_not_called()

    def test_local_model_parent_component_is_rejected_like_runtime(self) -> None:
        self.write_local_model("local", "model.onnx")
        local = self.write_descriptor("local", "nested/../model.onnx")
        opchain = self.write_opchain("local", [local])

        with mock.patch.object(model_materializer.subprocess, "run") as run:
            with self.assertRaisesRegex(
                model_materializer.MaterializationError,
                "Local modelFile .* is unsafe",
            ):
                self.materialize([opchain])

        run.assert_not_called()

    def test_rejects_non_file_published_asset_before_download(self) -> None:
        bundle = self.write_descriptor(
            "bundle",
            "hf:Arm/example@0123456789abcdef0123456789abcdef01234567#bundle",
        )
        opchain = self.write_opchain("bundle", [bundle])

        with mock.patch.object(model_materializer.subprocess, "run") as run:
            with self.assertRaisesRegex(
                model_materializer.MaterializationError, "one file asset"
            ):
                self.materialize([opchain])

        run.assert_not_called()

    def test_delegates_canonical_locator_validation_to_modelfetch(self) -> None:
        malformed = self.write_descriptor("remote", "hf:not-canonical#file=model.onnx")
        opchain = self.write_opchain("remote", [malformed])
        failure = subprocess.CompletedProcess(
            [], 2, "", "Asset ID must be a canonical pinned locator"
        )

        with mock.patch.object(
            model_materializer.subprocess, "run", return_value=failure
        ) as run:
            with self.assertRaisesRegex(
                model_materializer.MaterializationError, "canonical pinned locator"
            ):
                self.materialize([opchain])

        run.assert_called_once()

    def test_rejects_results_that_do_not_preserve_request_order(self) -> None:
        first = self.write_descriptor(
            "first",
            "hf:Arm/a@0123456789abcdef0123456789abcdef01234567#file=a.onnx",
        )
        second = self.write_descriptor(
            "second",
            "hf:Arm/b@0123456789abcdef0123456789abcdef01234567#file=b.onnx",
        )
        opchain = self.write_opchain("remote", [first, second])

        def reordered(
            command: list[str], **kwargs: object
        ) -> subprocess.CompletedProcess[str]:
            result = self.successful_download(command, **kwargs)
            document = json.loads(result.stdout)
            document["results"].reverse()
            return subprocess.CompletedProcess(command, 0, json.dumps(document), "")

        with mock.patch.object(
            model_materializer.subprocess, "run", side_effect=reordered
        ):
            with self.assertRaisesRegex(
                model_materializer.MaterializationError, "request order"
            ):
                self.materialize([opchain])

    def test_rejects_outdated_success_schema(self) -> None:
        descriptor = self.write_descriptor(
            "remote",
            "hf:Arm/a@0123456789abcdef0123456789abcdef01234567#file=a.onnx",
        )
        opchain = self.write_opchain("remote", [descriptor])

        def outdated(
            command: list[str], **kwargs: object
        ) -> subprocess.CompletedProcess[str]:
            result = self.successful_download(command, **kwargs)
            document = json.loads(result.stdout)
            document["results"][0]["destination"] = str(self.workspace / "var/models")
            return subprocess.CompletedProcess(command, 0, json.dumps(document), "")

        with mock.patch.object(
            model_materializer.subprocess, "run", side_effect=outdated
        ):
            with self.assertRaisesRegex(
                model_materializer.MaterializationError, "invalid success result"
            ):
                self.materialize([opchain])

    def test_snapshot_failure_reports_credential_contract(self) -> None:
        asset_id = "hf:Arm/a@0123456789abcdef0123456789abcdef01234567#file=a.onnx"
        descriptor = self.write_descriptor("remote", asset_id)
        opchain = self.write_opchain("remote", [descriptor])
        response = {
            "results": [
                {
                    "asset_id": asset_id,
                    "status": "failed",
                    "reason": "snapshot_unavailable",
                }
            ]
        }

        with mock.patch.object(
            model_materializer.subprocess,
            "run",
            return_value=subprocess.CompletedProcess([], 1, json.dumps(response), ""),
        ):
            with self.assertRaisesRegex(
                model_materializer.MaterializationError, "HF_TOKEN or HF_TOKEN_PATH"
            ):
                self.materialize([opchain])


if __name__ == "__main__":
    unittest.main()
