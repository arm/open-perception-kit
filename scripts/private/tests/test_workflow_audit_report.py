################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import sys
from pathlib import Path
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
from agent_workflow_test_support import (  # noqa: E402
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
