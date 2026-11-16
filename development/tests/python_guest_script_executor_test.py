#!/usr/bin/env python3
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

import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT
from open_perception_kit.packet import decode

EXECUTOR = Path(sys.argv.pop(1)).resolve()
SEED_SCRIPT = Path(sys.argv.pop(1)).resolve()
PROCESSOR_SCRIPT = Path(sys.argv.pop(1)).resolve()
OUTPUT_FILENAME = "results.bin"


def text(value: bytes | str) -> str:
    return value.decode("utf-8") if isinstance(value, bytes) else value


class PythonGuestScriptExecutorTest(unittest.TestCase):
    def run_executor(
        self, output: Path, *scripts: Path, python_paths: tuple[Path, ...] = ()
    ) -> subprocess.CompletedProcess[str]:
        path_arguments = [
            argument
            for python_path in python_paths
            for argument in ("--python-path", str(python_path))
        ]
        return subprocess.run(
            [
                str(EXECUTOR),
                "--output",
                str(output),
                *path_arguments,
                *(str(script) for script in scripts),
            ],
            check=False,
            capture_output=True,
            text=True,
        )

    def write_script(self, directory: Path, name: str, source: str) -> Path:
        path = directory / name
        path.write_text(textwrap.dedent(source), encoding="utf-8")
        return path

    def test_executes_ordered_chain_and_serializes_packet(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / OUTPUT_FILENAME
            result = self.run_executor(output, SEED_SCRIPT, PROCESSOR_SCRIPT)

            self.assertEqual(result.returncode, 0, result.stderr)
            envelope = decode(output.read_bytes())
            self.assertTrue(envelope.valid(), envelope.error())
            self.assertEqual(envelope.count(BoxDetectionsT), 2)

            original = envelope.get(BoxDetectionsT, 0)
            transformed = envelope.get(BoxDetectionsT, 1)
            self.assertEqual(text(original.layer.contentType), "test/input-boxes")
            self.assertEqual(original.detections[0].box.x, 10.0)
            self.assertEqual(original.detections[0].box.width, 30.0)
            self.assertEqual(text(original.detections[0].text), "seed")

            self.assertEqual(text(transformed.layer.contentType), "test/scaled-boxes")
            self.assertEqual(transformed.detections[0].object.id, 7)
            self.assertEqual(transformed.detections[0].object.parentId, 7)
            self.assertEqual(transformed.detections[0].box.x, 20.0)
            self.assertEqual(transformed.detections[0].box.y, 40.0)
            self.assertEqual(transformed.detections[0].box.width, 60.0)
            self.assertEqual(transformed.detections[0].box.height, 80.0)
            self.assertAlmostEqual(transformed.detections[0].confidence, 0.85, places=5)
            self.assertEqual(text(transformed.detections[0].text), "scaled")

    def test_rejects_missing_process_function(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            script = self.write_script(directory, "missing_process.py", "VALUE = 1\n")
            output = directory / OUTPUT_FILENAME

            result = self.run_executor(output, script)

            self.assertEqual(result.returncode, 4)
            self.assertIn("must define callable process(env)", result.stderr)
            self.assertFalse(output.exists())

    def test_classification_roots_and_label_sets_cross_the_live_bridge(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            imports = """
                from open_perception_kit.fb.open_perception_kit.metadata.Classifications import ClassificationsT
                from open_perception_kit.fb.open_perception_kit.metadata.Classification import ClassificationT
                from open_perception_kit.fb.open_perception_kit.metadata.ClassificationCandidate import ClassificationCandidateT
                from open_perception_kit.fb.open_perception_kit.metadata.ClassificationKind import ClassificationKind
                from open_perception_kit.fb.open_perception_kit.metadata.CameraContacts import CameraContactsT
                from open_perception_kit.fb.open_perception_kit.metadata.CameraContact import CameraContactT
                from open_perception_kit.fb.open_perception_kit.metadata.CameraContactResult import CameraContactResultT
                from open_perception_kit.fb.open_perception_kit.metadata.PersonPresences import PersonPresencesT
                from open_perception_kit.fb.open_perception_kit.metadata.PersonPresence import PersonPresenceT
                from open_perception_kit.fb.open_perception_kit.metadata.PersonPresenceResult import PersonPresenceResultT
            """
            seed = self.write_script(directory, "seed_classifications.py", textwrap.dedent(imports) + """
def process(env):
    env.add(ClassificationsT(classifications=[ClassificationT(labelSet='test:labels', kind=ClassificationKind.Categorical, subjectId=42,
        candidates=[ClassificationCandidateT(confidence=.75, classId=7, text='文字'),
                    ClassificationCandidateT(confidence=.2, classId=0, text='zero')]),
        ClassificationT(kind=ClassificationKind.RecognizedText, candidates=[ClassificationCandidateT(confidence=.9, text='recognized text')])]))
    env.add(CameraContactsT(contacts=[
        CameraContactT(subjectId=42, result=CameraContactResultT(inContact=True, modelClassIndex=0, confidence=.5)),
        CameraContactT()]))
    env.add(PersonPresencesT(presences=[PersonPresenceT(subjectId=42, result=PersonPresenceResultT(personPresent=True, yesScore=2, noScore=-1)),
        PersonPresenceT(), PersonPresenceT(result=PersonPresenceResultT(personPresent=False, yesScore=0, noScore=1))]))
""")
            inspect = self.write_script(directory, "inspect_classifications.py", textwrap.dedent(imports) + """
def process(env):
    classification = env.get(ClassificationsT).classifications[0]
    assert classification.labelSet == 'test:labels'
    assert classification.kind == ClassificationKind.Categorical and classification.subjectId == 42
    assert classification.candidates[0].classId == 7
    assert classification.candidates[0].text == '文字'
    assert classification.candidates[1].classId == 0
    assert env.get(ClassificationsT).classifications[1].candidates[0].classId is None
    contact = env.get(CameraContactsT)
    assert contact.contacts[0].subjectId == 42 and contact.contacts[0].result.inContact
    assert contact.contacts[0].result.modelClassIndex == 0
    assert contact.contacts[1].result is None
    presence = env.get(PersonPresencesT).presences[0]
    assert presence.subjectId == 42 and presence.result.personPresent
    assert presence.result.yesScore == 2 and presence.result.noScore == -1
    assert env.get(PersonPresencesT).presences[1].result is None
    assert env.get(PersonPresencesT).presences[2].result.yesScore == 0
""")
            output = directory / OUTPUT_FILENAME
            result = self.run_executor(output, seed, inspect)
            self.assertEqual(result.returncode, 0, result.stderr)
            envelope = decode(output.read_bytes())
            self.assertTrue(envelope.valid(), envelope.error())

    def test_rejects_non_callable_process(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            script = self.write_script(directory, "non_callable.py", "process = 42\n")
            output = directory / OUTPUT_FILENAME

            result = self.run_executor(output, script)

            self.assertEqual(result.returncode, 4)
            self.assertIn("must define callable process(env)", result.stderr)
            self.assertFalse(output.exists())

    def test_reports_syntax_error(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            script = self.write_script(directory, "syntax_error.py", "def process(env)\n")
            output = directory / OUTPUT_FILENAME

            result = self.run_executor(output, script)

            self.assertEqual(result.returncode, 4)
            self.assertIn(str(script), result.stderr)
            self.assertIn("SyntaxError", result.stderr)
            self.assertFalse(output.exists())

    def test_reports_python_exception_with_script_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            script = self.write_script(
                directory,
                "raises.py",
                """
                def process(env):
                    raise ValueError("expected failure")
                """,
            )
            output = directory / OUTPUT_FILENAME

            result = self.run_executor(output, script)

            self.assertEqual(result.returncode, 4)
            self.assertIn(str(script), result.stderr)
            self.assertIn("ValueError: expected failure", result.stderr)
            self.assertFalse(output.exists())

    def test_rejects_non_none_return_value(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            script = self.write_script(
                directory,
                "returns_value.py",
                """
                def process(env):
                    return 42
                """,
            )
            output = directory / OUTPUT_FILENAME

            result = self.run_executor(output, script)

            self.assertEqual(result.returncode, 4)
            self.assertIn("process(env) must return None", result.stderr)
            self.assertFalse(output.exists())

    def test_rejects_missing_script(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            output = directory / OUTPUT_FILENAME
            missing = directory / "missing.py"

            result = self.run_executor(output, missing)

            self.assertEqual(result.returncode, 4)
            self.assertIn("failed to open guest script", result.stderr)
            self.assertFalse(output.exists())

    def test_reports_output_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            result = self.run_executor(directory, SEED_SCRIPT)

            self.assertEqual(result.returncode, 5)
            self.assertIn("failed to open output packet", result.stderr)

    def test_adds_explicit_python_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            module_directory = directory / "modules"
            module_directory.mkdir()
            self.write_script(module_directory, "fixture_value.py", "VALUE = 17\n")
            script = self.write_script(
                directory,
                "imports_fixture.py",
                """
                from fixture_value import VALUE

                def process(env):
                    if VALUE != 17:
                        raise AssertionError(VALUE)
                """,
            )
            output = directory / OUTPUT_FILENAME

            result = self.run_executor(output, script, python_paths=(module_directory,))

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(decode(output.read_bytes()).valid())

    def test_requires_output_option(self) -> None:
        result = subprocess.run(
            [str(EXECUTOR), str(SEED_SCRIPT)],
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("--output is required", result.stderr)

    def test_requires_at_least_one_script(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / OUTPUT_FILENAME
            result = self.run_executor(output)

            self.assertEqual(result.returncode, 2)
            self.assertIn("at least one guest script is required", result.stderr)

    def test_rejects_invalid_python_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            output = directory / OUTPUT_FILENAME
            result = self.run_executor(
                output,
                SEED_SCRIPT,
                python_paths=(directory / "missing-modules",),
            )

            self.assertEqual(result.returncode, 3)
            self.assertIn("Python path is not a directory", result.stderr)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
