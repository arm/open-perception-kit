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

"""The common envelope wire contract compiled by every language backend."""

ENVELOPE_SCHEMA_TEMPLATE = """namespace __SDK_NAME__.internalfb;

table WirePayload {
  id:ulong;
  blob:[ubyte];
}

table WireEnvelope {
  payloads:[WirePayload];
  producer_sdk_name:string;
  producer_sdk_version:string;
  producer_schema_set_sha256:string;
}

root_type WireEnvelope;
file_identifier "FLWD";
"""
