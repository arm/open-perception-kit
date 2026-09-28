################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


class PythonRuntimeAttachmentTests(unittest.TestCase):
    def test_repeated_operators_attach_to_host_interpreter(self) -> None:
        module_path = Path(sys.argv[1])
        spec = importlib.util.spec_from_file_location(
            "opk_python_runtime_attachment_test", module_path
        )
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        module.run()
        module.run()

        self.assertIn("open_perception_kit_bridge", sys.modules)
        self.assertIn("opk_python_ops", sys.modules)
        self.assertTrue(hasattr(sys.modules["open_perception_kit_bridge"], "Envelope"))
        self.assertTrue(hasattr(sys.modules["opk_python_ops"], "Tensor"))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
