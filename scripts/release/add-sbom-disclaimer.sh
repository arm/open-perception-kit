#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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
# Adds the Arm SBOM disclaimer to a CycloneDX JSON document, as a
# metadata property, without disturbing the rest of the document.
################################################################

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <path-to-sbom.json>" >&2
    exit 2
fi

sbom_path="$1"

disclaimer='THIS SOFTWARE BILL OF MATERIALS ("SBOM") IS PROVIDED BY ARM LIMITED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT ARE DISCLAIMED. IN NO EVENT SHALL ARM LIMITED BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SBOM, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.'

tmp_path="$(mktemp)"
trap 'rm -f "$tmp_path"' EXIT

jq --arg disclaimer "$disclaimer" \
    '.metadata.properties = ((.metadata.properties // []) + [{"name": "arm:sbom-disclaimer", "value": $disclaimer}])' \
    "$sbom_path" > "$tmp_path"

mv "$tmp_path" "$sbom_path"
