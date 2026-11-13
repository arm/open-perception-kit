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

"""Prepare the image pipelines used by nightly smoke tests."""

import argparse
import json
import shlex
import subprocess
from pathlib import Path
from string import Template


def image_pipeline(preset):
    stages = preset["pipeline"]
    inference = [index for index, stage in enumerate(stages) if stage.lstrip().startswith("opkinfer ")]
    if len(inference) != 1 or not preset["alternative-source-image"]:
        raise ValueError("Expected one inference stage and a nonempty image source")
    return {
        "version": preset["version"],
        "description": "Nightly packaged image-source smoke test",
        "pipeline": preset["alternative-source-image"] + stages[inference[0]:],
        "loop": False,
    }


def cascade_pipeline(opk_data, image, results):
    preset = json.loads(Path(__file__).with_name("nightly-deployment-pipeline.json").read_text(encoding="utf-8"))
    preset["pipeline"] = [Template(stage).substitute(opk_data=opk_data, image=image, results=results)
                          for stage in preset["pipeline"]]
    return preset


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    image = commands.add_parser("image", help="Use a packaged preset's image source")
    image.add_argument("source", type=Path)
    image.add_argument("output", type=Path)
    cascade = commands.add_parser("cascade", help="Run the finite cascade or write its pipeline JSON")
    cascade.add_argument("opk_data")
    cascade.add_argument("image")
    cascade.add_argument("results")
    cascade.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.command == "image":
        preset = image_pipeline(json.loads(args.source.read_text(encoding="utf-8")))
    else:
        preset = cascade_pipeline(args.opk_data, args.image, args.results)
    if args.output is not None:
        args.output.write_text(json.dumps(preset), encoding="utf-8")
    else:
        pipeline_args = shlex.split(" ".join(preset["pipeline"]))
        subprocess.run(["timeout", "120s", "gst-launch-1.0", "-q", *pipeline_args], check=True)


if __name__ == "__main__":
    main()
