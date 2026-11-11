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

set -euo pipefail

export COVERAGE_RCFILE="$PWD/scripts/health-report/coverage.ini"
export COVERAGE_FILE="${COVERAGE_FILE:-$PWD/.coverage}"

python -m coverage erase
python -m coverage run -m unittest scripts.release.TestReleaseTool
for directory in scripts/testing scripts/testing/valgrind scripts/private/tests scripts/private/executorch tools/opk-ci/tests; do
    python -m coverage run -m unittest discover -s "$directory" -p 'test_*.py'
done
python -m coverage combine
python -m coverage json -o "$1"
