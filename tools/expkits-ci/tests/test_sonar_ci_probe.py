################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

import unittest

from expkits_ci.expkits_ci import sonar_ci_probe


class SonarCiProbeTests(unittest.TestCase):
    def test_sonar_ci_probe_executes_probe_command(self):
        self.assertEqual(sonar_ci_probe("true"), 0)


if __name__ == "__main__":
    unittest.main()
