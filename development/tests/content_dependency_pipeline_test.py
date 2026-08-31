#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Verify that enabling a model activates its upstream content providers."""

from __future__ import annotations

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

        descriptors = self.create_dependency_opchains()
        self.pipeline = self.create_pipeline(descriptors)
        self.addCleanup(self.stop_pipeline)
        self.elements = {
            name: self.pipeline.get_by_name(name) for name in descriptors
        }

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

    def create_pipeline(self, descriptors: dict[str, Path]):
        inference_chain = " ! ".join(
            f'pekinfer name={name} opchain-path="{descriptor}" active=false'
            for name, descriptor in descriptors.items()
        )
        return self.Gst.parse_launch(
            "appsrc is-live=true format=time "
            "caps=video/x-raw,format=BGRA,width=16,height=16 ! "
            f"{inference_chain} ! fakesink async=false sync=false"
        )

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
        self.start_pipeline()

        # Enabling gaze requests cameraContact upstream. Contact activates and in
        # turn requests humanFace, which activates every matching face provider.
        self.elements["gaze"].set_property("active", True)

        for name in ("gaze", "contact", "face-a", "face-b"):
            self.assertTrue(self.elements[name].get_property("active"), name)
        self.assertFalse(self.elements["unrelated"].get_property("active"))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
