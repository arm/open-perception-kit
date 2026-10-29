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

project('lrt-host-conformance', 'cpp', default_options : ['cpp_std=c++17', 'warning_level=3'])

gtest = dependency('gtest_main', required : true)
provider_sources = get_option('provider_sources')
using_mock = provider_sources.length() == 0
if using_mock
  provider_sources = ['mock_runtime.cpp']
endif
conformance = executable(
  'loom_host_conformance_tests',
  ['host_conformance_test.cpp'] + provider_sources,
  dependencies : gtest,
)
test('host-conformance', conformance, timeout : 60)
if using_mock
  test('detect-wrong-output', conformance,
    args : ['--gtest_filter=HostConformance.RepeatedHostExecution'],
    env : ['LRT_MOCK_FAULT=wrong-output'], should_fail : true, timeout : 10)
  test('detect-clobbered-bootstrap', conformance,
    args : ['--gtest_filter=HostConformance.BootstrapVersionMismatchPreservesTable'],
    env : ['LRT_MOCK_FAULT=clobber-bootstrap'], should_fail : true, timeout : 10)
endif
