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

from __future__ import annotations

import sys
from pathlib import Path
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from test_support.agent_workflow import (  # noqa: E402
    WORKFLOW_AUDIT_REPORT,
)


class WorkflowAuditReportTests(unittest.TestCase):
    def test_workflow_audit_fetch_latest_ref_uses_shared_github_api_client(self):
        with mock.patch.object(
            WORKFLOW_AUDIT_REPORT,
            "github_api_json_or_empty",
            side_effect=[{}, [{"name": "v6"}]],
        ) as github_api_json_or_empty:
            latest_ref, latest_source = WORKFLOW_AUDIT_REPORT.fetch_latest_ref(
                None,
                "actions/checkout",
            )

        self.assertEqual(latest_ref, "v6")
        self.assertEqual(latest_source, "tag")
        self.assertEqual(
            [call.args[0] for call in github_api_json_or_empty.call_args_list],
            [
                "repos/actions/checkout/releases/latest",
                "repos/actions/checkout/tags?per_page=1",
            ],
        )
        self.assertEqual(
            [call.kwargs["token"] for call in github_api_json_or_empty.call_args_list],
            [None, None],
        )


if __name__ == "__main__":
    unittest.main()
