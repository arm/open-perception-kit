#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
cd "${repo_root}"

gcovr -r . \
    --exclude-noncode-lines \
    --exclude-throw-branches \
    --exclude-unreachable-branches \
    --sonarqube coverage.xml
sed -E -i 's/ branchesToCover="[0-9]+" coveredBranches="[0-9]+"//g' coverage.xml

coverage erase
coverage run --parallel-mode --branch --source=tools/perception,scripts/release \
    -m unittest tools/perception/tests/test_release.py
coverage run --parallel-mode --branch --source=tools/perception,scripts/release \
    -m unittest scripts/release/TestReleaseTool.py
coverage run --parallel-mode development/tests/python_classification_demo_test.py
PYTHONPATH="generated/perception/python/src${PYTHONPATH:+:${PYTHONPATH}}" \
    coverage run --parallel-mode --branch --source=examples/byom-blazeface \
    development/tests/byom_blazeface_example_test.py
PYTHONPATH="tools/expkits-ci:tools/expkits-ci/tests${PYTHONPATH:+:${PYTHONPATH}}" \
    coverage run --parallel-mode --branch --source=tools/expkits-ci/expkits_ci \
    -m unittest \
    test_clang_tidy_statistics \
    test_config_schema_check \
    test_detect_secrets_quality_flow.DetectSecretsQualityFlowTests \
    test_expkits_ci_cli \
    test_expkits_ci_e2e \
    test_quality_checks
PYTHONPATH="tools/plumber${PYTHONPATH:+:${PYTHONPATH}}" \
    coverage run --parallel-mode --branch --source=tools/plumber/plumber \
    -m unittest discover -s tools/plumber/tests -p 'test_*.py'
coverage run --parallel-mode --branch --source=scripts/testing/valgrind \
    --omit='scripts/testing/valgrind/test_*.py' \
    -m unittest discover -s scripts/testing/valgrind -p 'test_*.py'
coverage combine
coverage xml -o python-coverage.xml

rm -rf development/web/coverage
mkdir -p development/web/coverage
PEK_WEB_TEST_OUTPUT_DIR=development/web/coverage \
    node development/web/test-frame-results.mjs \
    --enable-source-maps \
    --experimental-test-coverage \
    --test-reporter=spec \
    --test-reporter-destination=stdout \
    --test-reporter=lcov \
    --test-reporter-destination=development/web/coverage/lcov.info \
    development/web/tests/copy-utils.test.mjs \
    development/web/tests/osd-renderer.test.mjs \
    development/web/tests/video-layout.test.mjs \
    development/web/tests/webrtc_client.test.mjs \
    development/web/tests/webrtc_config.test.mjs
