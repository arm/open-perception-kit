# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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


def distance_classification(gt_item: dict, out_item: dict, _gt_parent_item: dict, _out_parent_item) -> float:
    """
    Placeholder: compare top-1 text equality (0 if equal else 1).
    We'll implement normalized candidate overlap later.
    """

    del _gt_parent_item
    del _out_parent_item

    g_cands = gt_item.get("data", {}).get("candidates", [])
    o_cands = out_item.get("data", {}).get("candidates", [])
    if not g_cands or not o_cands:
        return 1.0
    gt_top = g_cands[0].get("text")
    out_top = o_cands[0].get("text")
    return 0.0 if gt_top == out_top else 1.0


def distance_person_presence(gt_item: dict, out_item: dict, _gt_parent_item: dict, _out_parent_item) -> float:
    del _gt_parent_item
    del _out_parent_item

    gt_data = gt_item.get("data", {})
    out_data = out_item.get("data", {})
    yes_diff = abs(float(gt_data.get("yesConfidence", 0.0)) -
                   float(out_data.get("yesConfidence", 0.0)))
    no_diff = abs(float(gt_data.get("noConfidence", 0.0)) -
                  float(out_data.get("noConfidence", 0.0)))
    return max(0.0, min(1.0, max(yes_diff, no_diff)))
