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

import unittest
from types import SimpleNamespace
from test_frame_results_compare import frame_results_frame, frame_results_with_boxes, box_payload, layer
from plumber.frame_results_compare import compare_frame_results_frame
from plumber.frame_results_sdk import FrameResults
from plumber.tracking import State, Clock
from open_perception_kit.fb.open_perception_kit.metadata.ObjectMeta import ObjectMetaT
from open_perception_kit.fb.open_perception_kit.metadata.ObjectTrack import ObjectTrackT
from open_perception_kit.fb.open_perception_kit.metadata.ObjectTracks import ObjectTracksT
from open_perception_kit.fb.open_perception_kit.metadata.TrackIdentity import TrackIdentityT
from open_perception_kit.fb.open_perception_kit.metadata.TrackingSample import TrackingSampleT
from open_perception_kit.fb.open_perception_kit.metadata.TrackTrace import TrackTraceT
from open_perception_kit.fb.open_perception_kit.metadata.TrackTraces import TrackTracesT
from open_perception_kit.fb.open_perception_kit.metadata.TrackTracePoint import TrackTracePointT
from open_perception_kit.fb.open_perception_kit.metadata.TrackAssociation import TrackAssociationT


def sample(index, state=State.Observed):
    return TrackingSampleT(frameIndex=index, elapsedMs=index * 10, state=state, timeSource=Clock.MediaDelta)


def track(predicted=False, session="session", tracker="tracker", number=1, source=2):
    return ObjectTrackT(object=ObjectMetaT(id=50 + number), sourceId=source, sourceFrameIndex=1 if predicted else 2,
                        identity=TrackIdentityT(trackerId=tracker, sessionId=session, trackId=number),
                        sample=sample(2, State.Predicted if predicted else State.Observed), sourceConfidence=.9,
                        box=box_payload((source, 10, 10, 20, 20)).detections[0].box, text="person", classId=0)


def trace(owner, count=2):
    return TrackTraceT(identity=owner.identity, space=owner.box.space,
                       points=[TrackTracePointT(x=10 + index, y=20, sample=sample(index, owner.sample.state if index == count else State.Observed))
                               for index in range(1, count + 1)])


def packet(tracks=(), traces=(), source=True):
    result = frame_results_with_boxes((2, 10, 10, 20, 20)) if source else FrameResults()
    if tracks:
        result.add(ObjectTracksT(layer=layer("tracker"), tracks=list(tracks)))
    if traces:
        result.add(TrackTracesT(layer=layer("tracker"), traces=list(traces)))
    return frame_results_frame(FrameResults(result.serialize()))


class TrackingComparisonTests(unittest.TestCase):
    def check(self, a, b, matches):
        ok, message = compare_frame_results_frame(SimpleNamespace(verbose=False), a, b)
        self.assertEqual(ok, matches, message)

    def test_sessions_are_run_local_but_track_and_tracker_identity_are_not_ignored(self):
        self.check(packet([track()]), packet([track(session="another-run")]), True)
        unknown = track()
        unknown.association = TrackAssociationT()
        self.check(packet([track()]), packet([unknown]), True)
        for changes in ({"number": 2}, {"tracker": "other"}, {"predicted": True}):
            self.check(packet([track()]), packet([track(**changes)]), False)

    def test_predicted_sources_may_be_historical_but_observed_sources_must_resolve(self):
        self.check(packet([track(True)], source=False), packet(
            [track(True, session="other", source=999)], source=False), True)
        value = packet([track()], source=False)
        self.check(value, value, False)
        value = track(True)
        value.sourceFrameIndex = 2
        invalid = packet([value], source=False)
        self.check(invalid, invalid, False)

    def test_trace_length_state_timing_and_association_are_checked(self):
        owner = track()
        good = trace(owner)
        self.check(packet([owner], [good]), packet([owner], [good]), True)
        self.check(packet(traces=[good]), packet(traces=[trace(owner, 1)]), False)
        for field, value in (("elapsedMs", 5), ("frameIndex", 1), ("state", 0), ("timeSource", 0)):
            bad = trace(owner)
            setattr(bad.points[1].sample, field, value)
            invalid = packet(traces=[bad])
            self.check(invalid, invalid, False)
        stale = trace(owner)
        stale.identity = TrackIdentityT(trackerId="tracker", sessionId="previous-session", trackId=1)
        invalid = packet([owner], [stale])
        self.check(invalid, invalid, False)
        wrong_state = trace(owner)
        wrong_state.points[-1].sample.state = State.Predicted
        invalid = packet([owner], [wrong_state])
        self.check(invalid, invalid, False)

    def test_duplicate_identity_is_ambiguous_but_multiple_trackers_can_reuse_numbers(self):
        one, two = track(), track(tracker="second")
        self.check(packet([one, two], [trace(one), trace(two)]), packet([two, one], [trace(two), trace(one)]), True)
        duplicate = packet([one, one], [trace(one)])
        self.check(duplicate, duplicate, False)
        duplicate = packet([one], [trace(one), trace(one)])
        self.check(duplicate, duplicate, False)


if __name__ == "__main__":
    unittest.main()
