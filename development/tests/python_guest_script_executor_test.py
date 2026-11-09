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

    def test_tracking_identity_samples_and_trace_history_cross_the_live_bridge(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            imports = """
                from open_perception_kit.fb.open_perception_kit.metadata.ObjectTrack import ObjectTrackT
                from open_perception_kit.fb.open_perception_kit.metadata.ObjectTracks import ObjectTracksT
                from open_perception_kit.fb.open_perception_kit.metadata.TrackIdentity import TrackIdentityT
                from open_perception_kit.fb.open_perception_kit.metadata.TrackingSample import TrackingSampleT
                from open_perception_kit.fb.open_perception_kit.metadata.TrackTrace import TrackTraceT
                from open_perception_kit.fb.open_perception_kit.metadata.TrackTraces import TrackTracesT
                from open_perception_kit.fb.open_perception_kit.metadata.TrackTracePoint import TrackTracePointT
            """
            seed = self.write_script(directory, "seed_tracking.py", textwrap.dedent(imports) + """
def process(env):
    identity = TrackIdentityT(trackerId='tracker', sessionId='session', trackId=9007199254740993)
    sample = TrackingSampleT(frameIndex=2, elapsedMs=25, timeSource=1, state=2)
    env.add(ObjectTracksT(displayIdentity=False, tracks=[ObjectTrackT(identity=identity, sample=sample,
        sourceId=7, sourceFrameIndex=1, sourceConfidence=0, text='raw label')]))
    env.add(TrackTracesT(traces=[TrackTraceT(identity=identity,
        points=[TrackTracePointT(x=1, y=2, sample=sample)])]))
""")
            inspect = self.write_script(directory, "inspect_tracking.py", textwrap.dedent(imports) + """
def process(env):
    payload = env.get(ObjectTracksT)
    track = payload.tracks[0]
    assert payload.displayIdentity is False and track.text == 'raw label'
    assert track.identity.trackId == 9007199254740993 and track.identity.sessionId == 'session'
    assert track.sourceId == 7 and track.sourceFrameIndex == 1 and track.sourceConfidence == 0
    assert track.sample.state == 2 and track.sample.frameIndex == 2 and track.association is None
    point = env.get(TrackTracesT).traces[0].points[0]
    assert point.sample.elapsedMs == 25 and point.x == 1
""")
            output = directory / OUTPUT_FILENAME
            result = self.run_executor(output, seed, inspect)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(decode(output.read_bytes()).valid())

    def test_embedding_space_and_subject_cross_the_live_bridge(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            imports = """
                from open_perception_kit.fb.open_perception_kit.metadata.ObjectEmbedding import ObjectEmbeddingT
                from open_perception_kit.fb.open_perception_kit.metadata.ObjectEmbeddings import ObjectEmbeddingsT
                from open_perception_kit.fb.open_perception_kit.metadata.EmbeddingSpace import EmbeddingSpaceT
                from open_perception_kit.fb.open_perception_kit.metadata.EmbeddingMetric import EmbeddingMetric
            """
            seed = self.write_script(directory, "seed_embeddings.py", textwrap.dedent(imports) + """
def process(env):
    env.add(ObjectEmbeddingsT(embeddings=[ObjectEmbeddingT(subjectId=7, values=[3,4],
        space=EmbeddingSpaceT(id='test:space:v1', dimensions=2, metric=EmbeddingMetric.Cosine)),
        ObjectEmbeddingT(values=[])]))
""")
            inspect = self.write_script(directory, "inspect_embeddings.py", textwrap.dedent(imports) + """
def process(env):
    values = env.get(ObjectEmbeddingsT).embeddings
    assert values[0].subjectId == 7 and list(values[0].values) == [3,4]
    assert values[0].space.id == 'test:space:v1' and values[0].space.dimensions == 2
    assert values[0].space.metric == EmbeddingMetric.Cosine
    assert values[1].subjectId is None and values[1].space is None and len(values[1].values) == 0
""")
            output = directory / OUTPUT_FILENAME
            result = self.run_executor(output, seed, inspect)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(decode(output.read_bytes()).valid())

    def test_mask_encoding_meaning_and_optional_ids_cross_the_live_bridge(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            imports = """
                from open_perception_kit.fb.open_perception_kit.metadata.BitmapData import BitmapDataT
                from open_perception_kit.fb.open_perception_kit.metadata.BitmapEncoding import BitmapEncoding
                from open_perception_kit.fb.open_perception_kit.metadata.ImageSpace import ImageSpaceT
                from open_perception_kit.fb.open_perception_kit.metadata.OpacityMeaning import OpacityMeaning
                from open_perception_kit.fb.open_perception_kit.metadata.SegmentationMask import SegmentationMaskT
                from open_perception_kit.fb.open_perception_kit.metadata.SegmentationMasks import SegmentationMasksT
                from open_perception_kit.fb.open_perception_kit.metadata.SemanticSegmentationMask import SemanticSegmentationMaskT
                from open_perception_kit.fb.open_perception_kit.metadata.SemanticSegmentationMasks import SemanticSegmentationMasksT
                from open_perception_kit.fb.open_perception_kit.metadata.InstanceSegmentationMask import InstanceSegmentationMaskT
                from open_perception_kit.fb.open_perception_kit.metadata.InstanceSegmentationMasks import InstanceSegmentationMasksT
            """
            seed = self.write_script(directory, "seed_masks.py", textwrap.dedent(imports) + """
def process(env):
    bitmap = BitmapDataT(width=2, height=1, encoding=BitmapEncoding.Uint8, pixels=bytes([0, 1]),
        space=ImageSpaceT(frameId=7, width=100, height=80, originX=10, originY=20, extentWidth=30, extentHeight=40))
    env.add(SegmentationMasksT(masks=[SegmentationMaskT(bitmap=bitmap, meaning=OpacityMeaning.RegionOpacity)]))
    env.add(SemanticSegmentationMasksT(masks=[SemanticSegmentationMaskT(bitmap=bitmap, labelSet='test:labels', voidValue=0)]))
    env.add(InstanceSegmentationMasksT(masks=[InstanceSegmentationMaskT(bitmap=bitmap, labelSet='test:labels', classId=0, confidence=0),
        InstanceSegmentationMaskT(bitmap=bitmap)]))
""")
            inspect = self.write_script(directory, "inspect_masks.py", textwrap.dedent(imports) + """
def process(env):
    alpha = env.get(SegmentationMasksT).masks[0]
    assert alpha.meaning == OpacityMeaning.RegionOpacity
    assert alpha.bitmap.encoding == BitmapEncoding.Uint8
    assert list(alpha.bitmap.pixels) == [0, 1]
    assert alpha.bitmap.space.frameId == 7 and alpha.bitmap.space.originX == 10
    semantic = env.get(SemanticSegmentationMasksT).masks[0]
    assert semantic.labelSet == 'test:labels' and semantic.voidValue == 0
    instances = env.get(InstanceSegmentationMasksT).masks
    assert instances[0].classId == 0 and instances[0].confidence == 0
    assert instances[1].classId is None and instances[1].confidence is None
""")
            output = directory / OUTPUT_FILENAME
            result = self.run_executor(output, seed, inspect)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(decode(output.read_bytes()).valid())

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

    def test_gaze_optional_measurements_cross_the_live_bridge(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            imports = """
from open_perception_kit.fb.open_perception_kit.metadata.GazeAngles import GazeAnglesT
from open_perception_kit.fb.open_perception_kit.metadata.GazeMeasurement import GazeMeasurementT
from open_perception_kit.fb.open_perception_kit.metadata.GazeMeasurements import GazeMeasurementsT
"""
            seed = self.write_script(directory, "seed_gaze.py", imports + """
def process(env):
    env.add(GazeMeasurementsT(measurements=[GazeMeasurementT(),
        GazeMeasurementT(subjectId=9007199254740993, angles=GazeAnglesT(yawDegrees=0, pitchDegrees=0), confidence=0)]))
""")
            inspect = self.write_script(directory, "inspect_gaze.py", imports + """
def process(env):
    missing, zero = env.get(GazeMeasurementsT).measurements
    assert missing.angles is None and missing.subjectId is None and missing.confidence is None
    assert zero.subjectId == 9007199254740993
    assert zero.angles.yawDegrees == 0 and zero.angles.pitchDegrees == 0 and zero.confidence == 0
""")
            output = directory / OUTPUT_FILENAME
            result = self.run_executor(output, seed, inspect)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(decode(output.read_bytes()).valid())

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
