################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import sys
from pathlib import Path
import unittest
import io
import json
import os
import tempfile
import textwrap
import urllib.error
import urllib.parse
from typing import Any
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tests'))
from agent_workflow_test_support import (  # noqa: E402
    AGENT_REVIEW_COMMENTS,
    AGENT_REVIEW_DIFF_ANCHORS,
    AGENT_REVIEW_FETCH,
    AGENT_REVIEW_FETCH_SCRIPT,
    AGENT_REVIEW_GITHUB_PUBLISH,
    AGENT_REVIEW_MARKDOWN,
    AGENT_REVIEW_OUTPUT,
    AGENT_REVIEW_PROMPT,
    AGENT_REVIEW_PROMPT_TEMPLATE,
    AGENT_REVIEW_PUBLISH,
    AGENT_REVIEW_STATE,
    OPENAI_AGENT_CONTRACTS,
    REPAIR_BRANCH,
    build_zip_archive,
    http_headers,
)


class AgentRuntimeReviewPipelineTests(unittest.TestCase):
    def test_read_review_artifact_state_uses_safe_archive_extraction(self):
        archive = build_zip_archive(
            {
                "nested/review.json": json.dumps(
                    {
                        "overall_recommendation": "approve",
                        "summary": "Looks good.",
                    }
                )
            }
        )

        with mock.patch.object(
            AGENT_REVIEW_STATE,
            "github_api_json",
            return_value={
                "artifacts": [
                    {
                        "name": "agent-review-out",
                        "archive_download_url": "https://api.github.com/artifacts/1/zip",
                        "expired": False,
                    }
                ]
            },
        ):
            with mock.patch.object(AGENT_REVIEW_STATE, "download_github_archive", return_value=archive):
                review_state = AGENT_REVIEW_STATE.read_review_artifact_state(
                    repository="Arm-Debug/amp-dev-forge",
                    run_id="28000000001",
                    head_sha="deadbeef",
                )

        self.assertEqual(review_state["overall_recommendation"], "approve")
        self.assertEqual(review_state["run_id"], "28000000001")
        self.assertEqual(review_state["head_sha"], "deadbeef")

    def test_agent_review_output_drops_invalid_right_side_anchors(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            workflow_path = repo_root / ".github/workflows/workflow-audit.yml"
            workflow_path.parent.mkdir(parents=True)
            workflow_path.write_text("name: Workflow audit\njobs: {}\n", encoding="utf-8")

            payload = {
                "summary": "Reviewed workflow changes.",
                "overall_recommendation": "request_changes",
                "overall_score": 0.92,
                "overall_confidence": 0.87,
                "findings": [
                    {
                        "title": "Impossible stale workflow path",
                        "severity": "major",
                        "score": 0.92,
                        "confidence": 0.94,
                        "path": ".github/workflows/workflow-audit.yml",
                        "diff_side": "RIGHT",
                        "start_line": 367,
                        "end_line": 367,
                        "body": "This line does not exist in the current checkout.",
                        "suggestion": None,
                    },
                    {
                        "title": "Supported note",
                        "severity": "note",
                        "score": 0.32,
                        "confidence": 0.81,
                        "path": ".github/workflows/workflow-audit.yml",
                        "diff_side": "RIGHT",
                        "start_line": 1,
                        "end_line": 1,
                        "body": "This line exists in the current checkout.",
                        "suggestion": None,
                    },
                ],
            }

            filtered = AGENT_REVIEW_OUTPUT.filter_invalid_right_side_findings(payload, repo_root)

        self.assertEqual(filtered["overall_recommendation"], "comment")
        self.assertEqual(filtered["overall_score"], 0.32)
        self.assertEqual(
            filtered["summary"],
            (
                "Review kept 1 supported non-blocking finding. "
                "Omitted 1 unsupported review finding whose anchors are not supported by the current checkout or PR diff."
            ),
        )
        self.assertEqual([finding["title"] for finding in filtered["findings"]], ["Supported note"])

    def test_agent_review_output_drops_right_side_findings_outside_pr_diff(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            workflow_path = repo_root / ".github/workflows/pek-ci.yml"
            workflow_path.parent.mkdir(parents=True)
            workflow_path.write_text("\n".join(f"line {index}" for index in range(1, 310)), encoding="utf-8")

            payload = {
                "summary": "Reviewed workflow changes.",
                "overall_recommendation": "request_changes",
                "overall_score": 0.74,
                "overall_confidence": 0.82,
                "findings": [
                    {
                        "title": "Body-only blocking finding",
                        "severity": "major",
                        "score": 0.74,
                        "confidence": 0.82,
                        "path": ".github/workflows/pek-ci.yml",
                        "diff_side": "RIGHT",
                        "start_line": 296,
                        "end_line": 296,
                        "body": "The line exists in the checkout but is not part of the PR diff.",
                        "suggestion": "line 296",
                    },
                    {
                        "title": "Supported blocking finding",
                        "severity": "major",
                        "score": 0.72,
                        "confidence": 0.8,
                        "path": ".github/workflows/pek-ci.yml",
                        "diff_side": "RIGHT",
                        "start_line": 285,
                        "end_line": 285,
                        "body": "This line is part of the PR diff.",
                        "suggestion": None,
                    },
                ],
            }

            filtered = AGENT_REVIEW_OUTPUT.filter_invalid_right_side_findings(
                payload,
                repo_root,
                diff_anchors={(".github/workflows/pek-ci.yml", "RIGHT", 285)},
            )

        self.assertEqual(filtered["overall_recommendation"], "request_changes")
        self.assertEqual([finding["title"] for finding in filtered["findings"]], ["Supported blocking finding"])
        self.assertIn("PR diff", filtered["summary"])

    def test_agent_review_publish_filters_output_before_rendering(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            review_path = repo_root / "review.json"
            review_path.write_text(
                json.dumps(
                    {
                        "summary": "Reviewed workflow changes.",
                        "overall_recommendation": "request_changes",
                        "overall_score": 0.92,
                        "overall_confidence": 0.87,
                        "findings": [
                            {
                                "title": "Impossible stale workflow path",
                                "severity": "major",
                                "score": 0.92,
                                "confidence": 0.94,
                                "path": ".github/workflows/missing.yml",
                                "diff_side": "RIGHT",
                                "start_line": 1,
                                "end_line": 1,
                                "body": "This line does not exist in the current checkout.",
                                "suggestion": None,
                            },
                        ],
                    }
                ),
                encoding="utf-8",
            )

            filtered = AGENT_REVIEW_PUBLISH.load_filtered_review(review_path, repo_root)
            persisted = json.loads(review_path.read_text(encoding="utf-8"))

        self.assertEqual(filtered["overall_recommendation"], "approve")
        self.assertEqual(filtered["findings"], [])
        self.assertEqual(persisted, filtered)

    def test_agent_review_output_drops_known_available_action_ref_claims(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            workflow_path = repo_root / ".github/workflows/agent-review.yml"
            workflow_path.parent.mkdir(parents=True)
            workflow_path.write_text("uses: actions/checkout@v6\n", encoding="utf-8")

            payload = {
                "summary": "Reviewed workflow changes.",
                "overall_recommendation": "request_changes",
                "overall_score": 0.9,
                "overall_confidence": 0.96,
                "findings": [
                    {
                        "title": "Checkout action is non-existent",
                        "severity": "major",
                        "score": 0.9,
                        "confidence": 0.96,
                        "path": ".github/workflows/agent-review.yml",
                        "diff_side": "RIGHT",
                        "start_line": 1,
                        "end_line": 1,
                        "body": "The currently published major version is v4; actions/checkout@v6 is non-existent.",
                        "suggestion": "uses: actions/checkout@v4",
                    },
                ],
            }

            filtered = AGENT_REVIEW_OUTPUT.filter_invalid_right_side_findings(payload, repo_root)

        self.assertEqual(filtered["overall_recommendation"], "approve")
        self.assertEqual(filtered["findings"], [])

    def test_agent_review_output_drops_verified_model_availability_claims(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            model_config_path = repo_root / ".github/agent-runtime/runtime/agent-models.json"
            model_config_path.parent.mkdir(parents=True)
            model_config_path.write_text(
                json.dumps(
                    {
                        "default_agent_model": "gpt-5.5",
                        "agents": {
                            "review": {"model": "gpt-5.5"},
                            "repair": {"model": "gpt-5.5"},
                            "stabilization": {"model": "gpt-5.5"},
                        },
                    },
                    indent=2,
                ),
                encoding="utf-8",
            )

            payload = {
                "summary": "Reviewed workflow changes.",
                "overall_recommendation": "request_changes",
                "overall_score": 0.78,
                "overall_confidence": 0.86,
                "findings": [
                    {
                        "title": "Default agent model is set to an unavailable-looking future model",
                        "severity": "major",
                        "score": 0.78,
                        "confidence": 0.86,
                        "path": ".github/agent-runtime/runtime/agent-models.json",
                        "diff_side": "RIGHT",
                        "start_line": 1,
                        "end_line": 12,
                        "body": (
                            "All agent instances now default to `gpt-5.5`, so jobs will fail at runtime "
                            "unless the proxy exposes this exact model."
                        ),
                        "suggestion": None,
                    },
                ],
            }

            unverified = AGENT_REVIEW_OUTPUT.filter_invalid_right_side_findings(payload, repo_root)
            filtered = AGENT_REVIEW_OUTPUT.filter_invalid_right_side_findings(
                payload,
                repo_root,
                verified_model="gpt-5.5",
            )

        self.assertEqual(unverified, payload)
        self.assertEqual(filtered["overall_recommendation"], "approve")
        self.assertEqual(filtered["findings"], [])

    def test_agent_review_output_drops_verified_requirement_availability_claims(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            requirements_path = repo_root / ".github/agent-runtime/runtime/requirements-openai-agents.txt"
            requirements_path.parent.mkdir(parents=True)
            requirements_path.write_text(
                "\n".join(
                    [
                        "openai-agents==0.17.7",
                        "openai==2.44.0",
                        "pydantic==2.13.4",
                        "truststore==0.10.4",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            installed_versions = {
                "openai-agents": "0.17.7",
                "openai": "2.44.0",
                "pydantic": "2.13.4",
            }

            payload = {
                "summary": "Reviewed workflow changes.",
                "overall_recommendation": "request_changes",
                "overall_score": 0.78,
                "overall_confidence": 0.86,
                "findings": [
                    {
                        "title": "Pinned OpenAI runtime dependencies are unavailable",
                        "severity": "major",
                        "score": 0.78,
                        "confidence": 0.86,
                        "path": ".github/agent-runtime/runtime/requirements-openai-agents.txt",
                        "diff_side": "RIGHT",
                        "start_line": 1,
                        "end_line": 3,
                        "body": (
                            "openai-agents==0.17.7, openai==2.44.0, and pydantic==2.13.4 "
                            "are not valid published versions, so pip install will fail during "
                            "dependency installation."
                        ),
                        "suggestion": None,
                    },
                ],
            }

            with mock.patch.object(
                AGENT_REVIEW_OUTPUT.importlib_metadata,
                "version",
                side_effect=lambda name: installed_versions[name],
            ):
                filtered = AGENT_REVIEW_OUTPUT.filter_invalid_right_side_findings(payload, repo_root)

        self.assertEqual(filtered["overall_recommendation"], "approve")
        self.assertEqual(filtered["findings"], [])

    def test_agent_review_output_drops_verified_agent_runtime_artifact_context_claims(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            prompt_path = repo_root / ".github/agent-runtime/workflow-action-update-agent/prompts/repair-goal.md.in"
            prompt_path.parent.mkdir(parents=True)
            prompt_path.write_text(
                "\n".join(
                    [
                        "Read the generated context files first.",
                        "Then inspect `.agent-runtime/workflow-action-update-agent/source-run.log`.",
                        "Relevant text artifacts are under `.agent-runtime/workflow-action-update-agent/artifacts/`.",
                    ]
                ),
                encoding="utf-8",
            )

            payload = {
                "summary": "Reviewed workflow changes.",
                "overall_recommendation": "request_changes",
                "overall_score": 0.7,
                "overall_confidence": 0.9,
                "findings": [
                    {
                        "title": "Optional source artifacts are downloaded outside the agent context",
                        "severity": "major",
                        "score": 0.7,
                        "confidence": 0.9,
                        "path": ".github/agent-runtime/workflow-action-update-agent/prompts/repair-goal.md.in",
                        "diff_side": "RIGHT",
                        "start_line": 3,
                        "end_line": 3,
                        "body": (
                            "The artifact is outside the agent context, but omit the finding if "
                            "`.agent-runtime/workflow-action-update-agent/artifacts/...` is matched."
                        ),
                        "suggestion": None,
                    },
                ],
            }

            filtered = AGENT_REVIEW_OUTPUT.filter_invalid_right_side_findings(payload, repo_root)

        self.assertEqual(filtered["overall_recommendation"], "approve")
        self.assertEqual(filtered["findings"], [])

    def test_agent_review_output_keeps_artifact_context_claims_without_anchor_evidence(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            prompt_path = repo_root / ".github/agent-runtime/workflow-action-update-agent/prompts/repair-goal.md.in"
            prompt_path.parent.mkdir(parents=True)
            prompt_path.write_text(
                "\n".join(
                    [
                        "Read the generated context files first.",
                        "Then inspect `.agent-runtime/workflow-action-update-agent/source-run.log`.",
                    ]
                ),
                encoding="utf-8",
            )

            payload = {
                "summary": "Reviewed workflow changes.",
                "overall_recommendation": "request_changes",
                "overall_score": 0.7,
                "overall_confidence": 0.9,
                "findings": [
                    {
                        "title": "Optional source artifacts are downloaded outside the agent context",
                        "severity": "major",
                        "score": 0.7,
                        "confidence": 0.9,
                        "path": ".github/agent-runtime/workflow-action-update-agent/prompts/repair-goal.md.in",
                        "diff_side": "RIGHT",
                        "start_line": 2,
                        "end_line": 2,
                        "body": (
                            "The artifact is outside the agent context, but omit the finding if "
                            "`.agent-runtime/workflow-action-update-agent/artifacts/...` is matched."
                        ),
                        "suggestion": None,
                    },
                ],
            }

            filtered = AGENT_REVIEW_OUTPUT.filter_invalid_right_side_findings(payload, repo_root)

        self.assertEqual(filtered, payload)

    def test_agent_review_prompt_renderer_uses_runtime_context(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            template = temp_path / "review.md.in"
            output = temp_path / "review.prompt.md"
            template.write_text(
                "\n".join(
                    [
                        "@@REPOSITORY@@",
                        "@@BASE_REF@@",
                        "@@BASE_SHA@@",
                        "@@HEAD_SHA@@",
                        "@@PR_NUMBER@@",
                        "@@PR_TITLE@@",
                        "@@PR_URL@@",
                    ]
                ),
                encoding="utf-8",
            )

            with mock.patch.dict(
                os.environ,
                {
                    "REVIEW_REPOSITORY": "Arm-Debug/amp-dev-forge",
                    "REVIEW_BASE_REF": "origin/main",
                    "REVIEW_BASE_SHA": "base-sha",
                    "REVIEW_HEAD_SHA": "head-sha",
                    "REVIEW_PR_NUMBER": "101",
                    "REVIEW_PR_TITLE": "Line one\nline two",
                    "REVIEW_PR_URL": "https://github.com/Arm-Debug/amp-dev-forge/pull/101",
                },
                clear=False,
            ):
                AGENT_REVIEW_PROMPT.render_prompt(output_path=output, template_path=template)

            rendered = output.read_text(encoding="utf-8")

        self.assertIn("Arm-Debug/amp-dev-forge", rendered)
        self.assertIn("origin/main", rendered)
        self.assertIn("base-sha", rendered)
        self.assertIn("head-sha", rendered)
        self.assertIn("101", rendered)
        self.assertIn("Line one line two", rendered)
        self.assertIn("https://github.com/Arm-Debug/amp-dev-forge/pull/101", rendered)

    def test_agent_review_prompt_omits_unsupported_or_contradicted_claims(self):
        content = AGENT_REVIEW_PROMPT_TEMPLATE.read_text(encoding="utf-8")

        self.assertIn("Prefer complete coverage of concrete, verified issues", content)
        self.assertIn("unsupported or contradicted by the current checkout, omit it", content)
        self.assertIn("prefer omission over unsupported or contradicted findings", content)
        self.assertIn("<agent-review:suppress>", content)
        self.assertIn("<agent-review:suppress-begin>", content)
        self.assertNotIn("under-reporting is worse", content)

    def test_agent_review_publish_comments_do_not_carry_machine_state(self):
        review: dict[str, Any] = {
            "summary": "Looks fine with one minor note.",
            "overall_recommendation": "comment",
            "overall_score": 0.3,
            "overall_confidence": 0.9,
            "findings": [
                {
                    "title": "Minor note",
                    "severity": "note",
                    "score": 0.2,
                    "confidence": 0.8,
                    "path": ".github/workflows/example.yml",
                    "diff_side": "RIGHT",
                    "start_line": 12,
                    "end_line": 12,
                    "body": "Nit: keep names aligned.",
                    "suggestion": "name: Example",
                }
            ],
        }

        markdown = AGENT_REVIEW_MARKDOWN.format_markdown(
            review,
            run_id="28000000001",
            head_sha="deadbeef",
        )
        inline_comment = AGENT_REVIEW_COMMENTS.build_inline_comment_body(
            review["findings"][0],
            run_id="28000000001",
        )

        self.assertIn(OPENAI_AGENT_CONTRACTS.MARKER, markdown)
        self.assertFalse(hasattr(OPENAI_AGENT_CONTRACTS, "STATE_MARKER"))
        self.assertFalse(hasattr(OPENAI_AGENT_CONTRACTS, "INLINE_STATE_MARKER"))
        self.assertNotIn("agent-review-state", markdown)
        self.assertNotIn('"finding_count":1', markdown)
        self.assertIn("### Findings", markdown)
        self.assertIn("**Minor note**", markdown)
        self.assertIn("Location: `.github/workflows/example.yml:L12 (RIGHT)`", markdown)
        self.assertIn("Nit: keep names aligned.", markdown)
        self.assertIn(OPENAI_AGENT_CONTRACTS.INLINE_MARKER, inline_comment)
        self.assertNotIn("agent-review-inline-state", inline_comment)
        self.assertEqual(AGENT_REVIEW_STATE.EMPTY_REVIEW_STATE["overall_recommendation"], "")
        self.assertNotIn(
            'MARKER = "<!-- agent-review-comment -->"',
            AGENT_REVIEW_FETCH_SCRIPT.read_text(encoding="utf-8"),
        )
        self.assertNotIn(
            "agent-review-inline-state",
            AGENT_REVIEW_FETCH_SCRIPT.read_text(encoding="utf-8"),
        )

    def test_agent_review_publish_submits_inline_findings_with_review(self):
        finding = {
            "title": "Blocking note",
            "severity": "major",
            "score": 0.78,
            "confidence": 0.9,
            "path": ".github/workflows/example.yml",
            "diff_side": "RIGHT",
            "start_line": 12,
            "end_line": 14,
            "body": "Keep the blocking finding attached to the submitted review.",
            "suggestion": "name: Example\non: pull_request\njobs: {}",
        }
        calls = []

        def fake_github_api_request(url, token, method="GET", payload=None):
            calls.append(
                {
                    "url": url,
                    "token": token,
                    "method": method,
                    "payload": payload,
                }
            )
            return "{}"

        with mock.patch.object(
            AGENT_REVIEW_GITHUB_PUBLISH,
            "github_api_request",
            fake_github_api_request,
        ):
            AGENT_REVIEW_GITHUB_PUBLISH.create_pull_review(
                "Arm-Debug/amp-dev-forge",
                "101",
                "token",
                "review body",
                "request_changes",
                commit_id="deadbeef",
                findings=[finding],
                run_id="28000000001",
            )

        self.assertEqual(len(calls), 1)
        call = calls[0]
        self.assertEqual(
            call["url"],
            "repos/Arm-Debug/amp-dev-forge/pulls/101/reviews",
        )
        self.assertEqual(call["method"], "POST")
        self.assertEqual(call["token"], "token")
        self.assertEqual(call["payload"]["body"], "review body")
        self.assertEqual(call["payload"]["event"], "REQUEST_CHANGES")
        self.assertEqual(call["payload"]["commit_id"], "deadbeef")
        self.assertEqual(len(call["payload"]["comments"]), 1)
        comment = call["payload"]["comments"][0]
        self.assertEqual(comment["path"], ".github/workflows/example.yml")
        self.assertEqual(comment["line"], 14)
        self.assertEqual(comment["side"], "RIGHT")
        self.assertEqual(comment["start_line"], 12)
        self.assertEqual(comment["start_side"], "RIGHT")
        self.assertIn(OPENAI_AGENT_CONTRACTS.INLINE_MARKER, comment["body"])
        self.assertNotIn("agent-review-inline-state", comment["body"])

    def test_agent_review_publish_collapses_left_ranges_to_single_anchor(self):
        finding = {
            "title": "Deleted line note",
            "severity": "major",
            "score": 0.78,
            "confidence": 0.9,
            "path": ".github/workflows/example.yml",
            "diff_side": "LEFT",
            "start_line": 12,
            "end_line": 14,
            "body": "Anchor deleted-code findings without risking the whole review batch.",
        }

        comment = AGENT_REVIEW_COMMENTS.build_review_comment_payload(
            finding,
            run_id="28000000001",
        )

        self.assertEqual(comment["path"], ".github/workflows/example.yml")
        self.assertEqual(comment["line"], 14)
        self.assertEqual(comment["side"], "LEFT")
        self.assertNotIn("start_line", comment)
        self.assertNotIn("start_side", comment)
        self.assertIn(OPENAI_AGENT_CONTRACTS.INLINE_MARKER, comment["body"])

    def test_agent_review_publish_filters_inline_comments_against_local_diff(self):
        diff_text = textwrap.dedent(
            """\
            diff --git a/src/example.py b/src/example.py
            index 1111111..2222222 100644
            --- a/src/example.py
            +++ b/src/example.py
            @@ -10,3 +10,4 @@
             context
            -old_value = 1
            +new_value = 1
            +extra_value = 2
            """
        )
        diff_anchors = AGENT_REVIEW_DIFF_ANCHORS.parse_diff_comment_anchors(diff_text)
        findings = [
            {
                "title": "Valid right range",
                "severity": "major",
                "score": 0.78,
                "confidence": 0.9,
                "path": "src/example.py",
                "diff_side": "RIGHT",
                "start_line": 11,
                "end_line": 12,
                "body": "Both added lines are present in the diff.",
            },
            {
                "title": "Invalid right range",
                "severity": "major",
                "score": 0.78,
                "confidence": 0.9,
                "path": "src/example.py",
                "diff_side": "RIGHT",
                "start_line": 99,
                "end_line": 99,
                "body": "This stale line is not present in the diff.",
            },
            {
                "title": "Valid left line",
                "severity": "major",
                "score": 0.78,
                "confidence": 0.9,
                "path": "src/example.py",
                "diff_side": "LEFT",
                "start_line": 11,
                "end_line": 11,
                "body": "The deleted line is present in the diff.",
            },
        ]

        comments = AGENT_REVIEW_COMMENTS.build_review_comment_payloads(
            findings,
            run_id="28000000001",
            diff_anchors=diff_anchors,
        )

        self.assertIn(("src/example.py", "RIGHT", 10), diff_anchors)
        self.assertIn(("src/example.py", "LEFT", 11), diff_anchors)
        self.assertEqual([comment["line"] for comment in comments], [12, 11])
        self.assertEqual(comments[0]["side"], "RIGHT")
        self.assertEqual(comments[0]["start_line"], 11)
        self.assertEqual(comments[1]["side"], "LEFT")
        self.assertNotIn("start_line", comments[1])

    def test_agent_review_publish_does_not_fallback_after_diff_validated_anchors(self):
        finding = {
            "title": "Blocking note",
            "severity": "major",
            "score": 0.78,
            "confidence": 0.9,
            "path": ".github/workflows/example.yml",
            "diff_side": "RIGHT",
            "start_line": 12,
            "end_line": 12,
            "body": "A validated anchor should fail loudly if GitHub rejects it.",
        }
        calls = []

        def fake_submit_pull_review(repository, pr_number, token, payload):
            calls.append(payload)
            raise urllib.error.HTTPError(
                "https://api.github.com/repos/Arm-Debug/amp-dev-forge/pulls/101/reviews",
                422,
                "Validation Failed",
                hdrs=http_headers(),
                fp=io.BytesIO(b'{"message":"Validation Failed"}'),
            )

        with mock.patch.object(
            AGENT_REVIEW_GITHUB_PUBLISH,
            "submit_pull_review",
            fake_submit_pull_review,
        ):
            with self.assertRaises(urllib.error.HTTPError):
                AGENT_REVIEW_GITHUB_PUBLISH.create_pull_review(
                    "Arm-Debug/amp-dev-forge",
                    "101",
                    "token",
                    "review body",
                    "request_changes",
                    commit_id="deadbeef",
                    findings=[finding],
                    run_id="28000000001",
                    diff_anchors={(".github/workflows/example.yml", "RIGHT", 12)},
                )

        self.assertEqual(len(calls), 1)
        self.assertIn("comments", calls[0])

    def test_agent_review_publish_keeps_review_when_inline_anchor_is_rejected(self):
        finding = {
            "title": "Blocking note",
            "severity": "major",
            "score": 0.78,
            "confidence": 0.9,
            "path": ".github/workflows/example.yml",
            "diff_side": "RIGHT",
            "start_line": 12,
            "end_line": 12,
            "body": "Keep the blocking review even when an inline anchor is stale.",
        }
        calls = []

        def fake_submit_pull_review(repository, pr_number, token, payload):
            calls.append(
                {
                    "repository": repository,
                    "pr_number": pr_number,
                    "token": token,
                    "payload": payload,
                }
            )
            if len(calls) == 1:
                raise urllib.error.HTTPError(
                    "https://api.github.com/repos/Arm-Debug/amp-dev-forge/pulls/101/reviews",
                    422,
                    "Validation Failed",
                    hdrs=http_headers(),
                    fp=io.BytesIO(b'{"message":"Validation Failed"}'),
                )

        with mock.patch.object(
            AGENT_REVIEW_GITHUB_PUBLISH,
            "submit_pull_review",
            fake_submit_pull_review,
        ), mock.patch.object(AGENT_REVIEW_GITHUB_PUBLISH.sys, "stderr", io.StringIO()):
            AGENT_REVIEW_GITHUB_PUBLISH.create_pull_review(
                "Arm-Debug/amp-dev-forge",
                "101",
                "token",
                "review body",
                "request_changes",
                commit_id="deadbeef",
                findings=[finding],
                run_id="28000000001",
            )

        self.assertEqual(len(calls), 2)
        self.assertIn("comments", calls[0]["payload"])
        self.assertEqual(calls[1]["payload"]["body"], "review body")
        self.assertEqual(calls[1]["payload"]["event"], "REQUEST_CHANGES")
        self.assertEqual(calls[1]["payload"]["commit_id"], "deadbeef")
        self.assertNotIn("comments", calls[1]["payload"])

    def test_agent_review_fetch_reads_latest_review_artifact_only(self):
        artifact_state = {
            "run_id": "28000000001",
            "head_sha": "deadbeef",
            "overall_recommendation": "request_changes",
            "summary": "Artifact summary.",
            "findings": [
                {
                    "title": "Artifact finding",
                    "path": ".github/workflows/example.yml",
                    "body": "Only the artifact is machine state.",
                }
            ],
        }
        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = Path(temp_dir) / "review-state.json"
            with mock.patch.dict(
                os.environ,
                {
                    "GITHUB_TOKEN": "token",
                    "GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge",
                    "GITHUB_PR_NUMBER": "123",
                },
                clear=False,
            ):
                with mock.patch.object(
                    AGENT_REVIEW_FETCH,
                    "read_pr_details",
                    return_value={"repair_branch": REPAIR_BRANCH, "head_sha": "deadbeef"},
                ) as read_pr_details:
                    with mock.patch.object(
                        AGENT_REVIEW_FETCH,
                        "find_latest_workflow_run_for_head",
                        return_value="28000000001",
                    ) as find_latest_workflow_run_for_head:
                        with mock.patch.object(
                            AGENT_REVIEW_FETCH,
                            "read_review_artifact_state",
                            return_value=artifact_state,
                        ) as read_review_artifact_state:
                            with mock.patch.object(
                                AGENT_REVIEW_FETCH.sys,
                                "argv",
                                ["fetch.py", "--output", str(output_path)],
                            ):
                                AGENT_REVIEW_FETCH.main()

            state = json.loads(output_path.read_text(encoding="utf-8"))

        self.assertEqual(state["overall_recommendation"], "request_changes")
        self.assertEqual(state["findings"][0]["title"], "Artifact finding")
        read_pr_details.assert_called_once_with("123")
        find_latest_workflow_run_for_head.assert_called_once_with(
            repository="Arm-Debug/amp-dev-forge",
            workflow_file="agent-review.yml",
            branch=REPAIR_BRANCH,
            head_sha="deadbeef",
        )
        read_review_artifact_state.assert_called_once_with(
            repository="Arm-Debug/amp-dev-forge",
            run_id="28000000001",
            head_sha="deadbeef",
        )

    def test_review_state_artifact_source_drives_non_approve_with_findings(self):
        artifact_state = AGENT_REVIEW_STATE.normalize_review_state(
            {
                "run_id": "28000000001",
                "head_sha": "deadbeef",
                "overall_recommendation": "request_changes",
                "finding_count": 0,
                "findings": [
                    {
                        "title": "Unexpected inline finding",
                        "path": ".github/workflows/example.yml",
                        "body": "A non-approve fallback with count zero is inconsistent.",
                    }
                ],
            }
        )

        self.assertTrue(
            AGENT_REVIEW_STATE.review_state_can_drive_stabilization(
                artifact_state,
                source="artifact",
            )
        )

    def test_review_state_non_artifact_source_cannot_drive_non_approve_state(self):
        comment_state = AGENT_REVIEW_STATE.normalize_review_state(
            {
                "run_id": "28000000001",
                "head_sha": "deadbeef",
                "overall_recommendation": "request_changes",
                "finding_count": 1,
                "finding_count_available": True,
                "findings": [
                    {
                        "title": "Recovered finding",
                        "path": ".github/workflows/example.yml",
                        "body": "PR comment state is UI only.",
                    }
                ],
            }
        )
        self.assertFalse(
            AGENT_REVIEW_STATE.review_state_can_drive_stabilization(
                comment_state,
                source="pull request state",
            )
        )


if __name__ == "__main__":
    unittest.main()
