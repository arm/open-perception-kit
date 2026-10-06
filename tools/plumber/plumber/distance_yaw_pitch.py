# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

from . import auxiliary as aux


def distance_yawpitch(
        gt_item: dict, out_item: dict, _gt_parent_item: dict, _out_parent_item: dict
) -> float:
    """
    Normalize yaw/pitch difference into [0,1].
    For now return normalized max(|yaw_diff|, |pitch_diff|) / 180 (safe upper bound).
    """

    del _gt_parent_item
    del _out_parent_item

    g = gt_item.get("data", {})
    o = out_item.get("data", {})
    yaw_diff = abs(float(g.get("yaw", 0.0)) - float(o.get("yaw", 0.0)))
    pitch_diff = abs(float(g.get("pitch", 0.0)) - float(o.get("pitch", 0.0)))
    # angles are in degrees-ish; use 180 as safe normalization factor
    return aux.clamp01(max(yaw_diff, pitch_diff) / 180.0)
