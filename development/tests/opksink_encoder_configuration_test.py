#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

"""Verify that OpkSink configures VP8 for adaptive resource usage."""

from pathlib import Path
import sys

from pipeline_test_utils import load_gstreamer_plugins


def main() -> None:
    gst = load_gstreamer_plugins([Path(sys.argv[1])])

    import gi

    gi.require_version("GLib", "2.0")
    from gi.repository import GLib

    sink = gst.ElementFactory.make("opksink", "encoder_configuration_sink")
    assert sink is not None

    encoder = sink.get_by_name("vp8enc")
    assert encoder is not None
    assert encoder.get_property("target-bitrate") != 2_500_000
    assert encoder.get_property("bits-per-pixel") > 0

    expected_threads = min(max(GLib.get_num_processors() // 2, 1), 64)
    assert encoder.get_property("threads") == expected_threads


if __name__ == "__main__":
    main()
