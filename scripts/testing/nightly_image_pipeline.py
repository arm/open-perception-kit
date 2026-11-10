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

"""Use a packaged pipeline's image source for its nightly dry run."""

import json
import sys
from pathlib import Path


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


if __name__ == "__main__":
    source = Path(sys.argv[1])
    destination = Path(sys.argv[2])
    preset = json.loads(source.read_text(encoding="utf-8"))
    destination.write_text(json.dumps(image_pipeline(preset)), encoding="utf-8")
