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

import json
import os
import subprocess
import sys
import shlex
import tempfile
import unittest
from pathlib import Path

from scripts.testing.nightly_image_pipeline import cascade_pipeline, image_pipeline

SCRIPT = Path(__file__).with_name("nightly-deb-and-archive-smoke.sh")
DEPLOYMENT_SCRIPT = Path(__file__).with_name("nightly-deployment-entrypoint-smoke.sh")


class PackageSmokeTest(unittest.TestCase):
    def test_rejects_unsupported_package_kind_before_installing(self):
        result = subprocess.run(["bash", str(SCRIPT), "unknown"], capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn(b"Usage:", result.stderr)

    def test_packaged_preset_uses_its_image_source_and_inference(self):
        repo = Path(__file__).resolve().parents[2]
        preset = json.loads((repo / "config/pipelines/yolo26n-320.json").read_text())
        result = image_pipeline(preset)
        self.assertEqual(result["pipeline"][:4], preset["alternative-source-image"])
        self.assertTrue(result["pipeline"][4].startswith("opkinfer "))
        self.assertEqual(result["pipeline"][4:], preset["pipeline"][4:])
        self.assertNotIn("data/videos", " ".join(result["pipeline"]))
        self.assertFalse(result["loop"])

    def test_rejects_missing_or_ambiguous_inference(self):
        for stages in ([], ["opkinfer !", "opkinfer !"]):
            with self.subTest(stages=stages):
                with self.assertRaises(ValueError):
                    image_pipeline({"pipeline": stages, "alternative-source-image": ["filesrc !"]})

        with self.assertRaises(ValueError):
            image_pipeline({"pipeline": ["opkinfer !"], "alternative-source-image": []})

    def test_cascade_run_and_json_use_the_same_fixture(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            gst = root / "gst-launch-1.0"
            gst.write_text(
                f"#!{sys.executable}\nimport json, sys\n"
                f"open({str(root / 'args.json')!r}, 'w').write(json.dumps(sys.argv[1:]))\n",
                encoding="utf-8",
            )
            gst.chmod(0o755)
            arguments = [str(root / "config with spaces"), "/images/face.jpg", str(root / "results.ndjson")]
            helper = Path(__file__).with_name("nightly_image_pipeline.py")
            command = [sys.executable, str(helper), "cascade", *arguments]
            subprocess.run(command, env=dict(os.environ, PATH=f"{root}:{os.environ['PATH']}"), check=True)
            output = root / "pipeline.json"
            subprocess.run([*command, "--output", str(output)], check=True)
            preset = json.loads(output.read_text())
            expected = ["-q", *shlex.split(" ".join(preset["pipeline"]))]
            self.assertEqual(json.loads((root / "args.json").read_text()), expected)
            self.assertIn(f"opchain-path={arguments[0]}/opchains/nitec-resnet-18/opchain-executorch.json", expected)
            self.assertFalse(preset["loop"])


class DeploymentSmokeTest(unittest.TestCase):
    def test_pipeline_is_finite_and_captures_cascade_results(self):
        preset = cascade_pipeline("/work/config", "/work/data/images/GettyImages-1140581459-thumbnail.jpg",
                                  "/tmp/nightly-deployment-cascade.ndjson")
        pipeline = " ".join(preset["pipeline"])
        self.assertFalse(preset["loop"])
        self.assertIn("GettyImages-1140581459-thumbnail.jpg", pipeline)
        self.assertIn("imagefreeze num-buffers=1", pipeline)
        self.assertIn("nitec-resnet-18/opchain-executorch.json", pipeline)
        self.assertIn("file-name=/tmp/nightly-deployment-cascade.ndjson", shlex.split(pipeline))

    def test_deployment_smoke_propagates_failures_and_removes_container(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            models = root / "models"
            models.mkdir()
            (models / "ultraface-rfb-320").mkdir()
            (models / "nitec-resnet-18-executorch").mkdir()
            log = root / "docker-calls.txt"
            docker = root / "docker"
            docker.write_text(
                "#!/usr/bin/env bash\n"
                "printf '%s\\n' \"$*\" >> \"$FAKE_DOCKER_LOG\"\n"
                "case $1 in\n"
                "  create) echo test-container ;;\n"
                "  container) echo '[{\"Image\":\"sha256:abc\",\"SizeRw\":7}]' ;;\n"
                "  image) echo '[{\"Size\":100}]' ;;\n"
                "  start) exit \"${FAKE_START_STATUS:-0}\" ;;\n"
                "  cp) if [[ ${FAKE_COPY_FAIL:-0} == 1 ]]; then exit 1; fi; "
                "echo packet > \"$3\" ;;\n"
                "  run) exit \"${FAKE_CHECK_STATUS:-0}\" ;;\n"
                "  rm) exit 0 ;;\n"
                "esac\n",
                encoding="utf-8",
            )
            docker.chmod(0o755)
            for start, copy, check, expected in ((0, 0, 0, 0), (7, 0, 0, 7),
                                                 (0, 1, 0, 1), (0, 0, 9, 9)):
                with self.subTest(start=start, copy=copy, check=check):
                    log.write_text("", encoding="utf-8")
                    env = dict(os.environ, PATH=f"{root}:{os.environ['PATH']}",
                               FAKE_DOCKER_LOG=str(log), FAKE_START_STATUS=str(start),
                               FAKE_COPY_FAIL=str(copy), FAKE_CHECK_STATUS=str(check))
                    result = subprocess.run(
                        ["bash", str(DEPLOYMENT_SCRIPT), "test-image", str(models), str(root / "logs")],
                        env=env, capture_output=True,
                    )
                    self.assertEqual(result.returncode, expected, result.stderr.decode())
                    calls = log.read_text(encoding="utf-8").splitlines()
                    self.assertTrue(calls[0].startswith("create "))
                    self.assertIn("OPK_PIPELINE=/nightly/pipeline.json", calls[0])
                    self.assertNotIn("--entrypoint", calls[0])
                    self.assertIn("start --attach test-container", calls)
                    self.assertEqual(calls[-1], "rm -f test-container")
                    self.assertEqual(any(call.startswith("run ") for call in calls),
                                     start == 0 and copy == 0)


if __name__ == "__main__":
    unittest.main()
