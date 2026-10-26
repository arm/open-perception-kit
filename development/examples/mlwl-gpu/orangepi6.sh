#!/usr/bin/env bash
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

set -eu
cd -- "$(dirname -- "$0")/../../.."
gpu_runtime=${1:?Usage: orangepi6.sh /absolute/gpu-runtime /absolute/mlwl-sdk}
mlwl_sdk=${2:?Supply the MLWL SDK path}
vendor=0x13b5
export VK_DRIVER_FILES="$gpu_runtime/panfrost_icd.json"
export VK_LAYER_PATH="$gpu_runtime/emulation-float/share/vulkan/explicit_layer.d"
export LD_LIBRARY_PATH="$mlwl_sdk/lib64:$PWD/development/build-active/meson-out:\
$gpu_runtime/lib:$gpu_runtime/emulation-float/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
unset VK_ICD_FILENAMES VK_ADD_DRIVER_FILES VK_LOADER_DRIVERS_SELECT VK_LOADER_DRIVERS_DISABLE
unset VK_INSTANCE_LAYERS VK_LOADER_LAYERS_ENABLE VK_LOADER_LAYERS_DISABLE
for manifest in "$VK_DRIVER_FILES" \
    "$VK_LAYER_PATH/VkLayer_Graph.json" "$VK_LAYER_PATH/VkLayer_Tensor.json"; do
    if [ ! -r "$manifest" ]; then
        echo "Missing Vulkan manifest: $manifest" >&2
        exit 1
    fi
done
summary=$(vulkaninfo --summary)
printf '%s\n' "$summary"
printf '%s\n' "$summary" | awk -v vendor="$vendor" '
    $1 == "vendorID" { vendors++; correct_vendor = ($3 == vendor) }
    $1 == "deviceType" { devices++; integrated = ($3 == "PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU") }
    END { exit !(vendors == 1 && devices == 1 && correct_vendor && integrated) }
' || {
    echo "Expected one integrated GPU with vendor $vendor" >&2
    exit 1
}
VK_INSTANCE_LAYERS=VK_LAYER_ML_Graph_Emulation:VK_LAYER_ML_Tensor_Emulation \
    ./tools/opchain-exec config/models/ultraface-rfb-320-mlwl/opchain.json \
    data/images/GettyImages-1140581459-thumbnail.jpg
