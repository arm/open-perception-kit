#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Verify that enabling a model activates its upstream content providers."""

from __future__ import annotations

from collections.abc import Collection
import ctypes
import gc
from pathlib import Path
import sys
import tempfile
import unittest

from pipeline_test_utils import (
    load_gstreamer_plugins,
    release_pipeline,
    write_test_opchain,
)


TEST_OP_PATH = Path(sys.argv[1]).resolve()
GST_PLUGIN_PATHS = [Path(argument).resolve() for argument in sys.argv[2:]]


class ContentDependencyPipelineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        # The descriptor references this test-only Op through the OpChain plugin ABI.
        # Loading it explicitly makes the test independent of the process library path.
        cls.test_op_library = ctypes.CDLL(str(TEST_OP_PATH))
        cls.Gst = load_gstreamer_plugins(GST_PLUGIN_PATHS)

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory(prefix="pek-content-dependency-")
        self.addCleanup(self.directory.cleanup)

        self.descriptors = self.create_dependency_opchains()
        self.pipeline = None
        self.addCleanup(self.stop_pipeline)
        self.elements = {}

    def create_dependency_opchains(self) -> dict[str, Path]:
        # Pipeline order is upstream to downstream. Both face models can provide the
        # contact model's input, while the unrelated model provides the wrong type.
        capabilities = {
            "unrelated": {"provided-content-type": "genericObject"},
            "face-a": {"provided-content-type": "humanFace"},
            "face-b": {"provided-content-type": "humanFace"},
            "contact": {
                "required-content-type": "humanFace",
                "provided-content-type": "cameraContact",
            },
            "gaze": {"required-content-type": "cameraContact"},
        }
        return {
            name: write_test_opchain(self.directory.name, name, attributes)
            for name, attributes in capabilities.items()
        }

    def create_pipeline(self, initially_active: Collection[str] = ()) -> None:
        inference_chain = " ! ".join(
            f'pekinfer name={name} opchain-path="{descriptor}"'
            f'{"" if name in initially_active else " active=false"}'
            for name, descriptor in self.descriptors.items()
        )
        self.pipeline = self.Gst.parse_launch(
            "appsrc is-live=true format=time "
            "caps=video/x-raw,format=BGRA,width=16,height=16 ! "
            f"{inference_chain} ! fakesink async=false sync=false"
        )
        self.elements = {
            name: self.pipeline.get_by_name(name) for name in self.descriptors
        }

    def start_pipeline(self) -> None:
        self.assertNotEqual(
            self.pipeline.set_state(self.Gst.State.PLAYING),
            self.Gst.StateChangeReturn.FAILURE,
        )
        self.pipeline.get_state(self.Gst.SECOND)

    def stop_pipeline(self) -> None:
        release_pipeline(self.pipeline, self.Gst)
        self.pipeline = None
        gc.collect()

    def test_enabling_model_activates_all_matching_upstream_dependencies(self) -> None:
        self.create_pipeline()
        self.start_pipeline()

        # Enabling gaze requests cameraContact upstream. Contact activates and in
        # turn requests humanFace, which activates every matching face provider.
        self.elements["gaze"].set_property("active", True)

        for name in ("gaze", "contact", "face-a", "face-b"):
            self.assertTrue(self.elements[name].get_property("active"), name)
        self.assertFalse(self.elements["unrelated"].get_property("active"))

    def test_initially_active_model_activates_upstream_dependencies(self) -> None:
        # Gaze uses pekinfer's default active=true state; every other model starts
        # disabled so activation can only come from requirements emitted at startup.
        self.create_pipeline(initially_active={"gaze"})
        self.start_pipeline()

        for name in ("gaze", "contact", "face-a", "face-b"):
            self.assertTrue(self.elements[name].get_property("active"), name)
        self.assertFalse(self.elements["unrelated"].get_property("active"))

    def test_active_provider_propagates_its_dependencies(self) -> None:
        self.create_pipeline()
        self.start_pipeline()

        # Activating contact initially enables its face providers. Disable them again
        # to isolate the path where gaze finds contact already active.
        self.elements["contact"].set_property("active", True)
        self.elements["face-a"].set_property("active", False)
        self.elements["face-b"].set_property("active", False)

        self.elements["gaze"].set_property("active", True)

        self.assertTrue(self.elements["face-a"].get_property("active"))
        self.assertTrue(self.elements["face-b"].get_property("active"))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
