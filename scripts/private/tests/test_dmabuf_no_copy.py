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

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
PROBE = ROOT / "scripts/rpi-camera-dmabuf-probe.sh"
COPY_PROGRAM = r"""
#include <gst/video/video.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv) {
    gst_init(NULL, NULL);
    if (argc == 2 && strcmp(argv[1], "--version") == 0)
        return 0;
    const char *mode = getenv("OPK_DMABUF_TEST_MODE");
    if (strcmp(mode, "hold") == 0) {
        FILE *pids = fopen(getenv("OPK_DMABUF_TEST_PIDS"), "a");
        g_assert(pids != NULL);
        fprintf(pids, "%ld\n", (long)getpid());
        fclose(pids);
        for (;;)
            pause();
    }
    if (strcmp(mode, "none") == 0)
        return 0;

    GstVideoInfo info;
    g_assert(gst_video_info_set_format(&info, GST_VIDEO_FORMAT_RGBA, 4, 4));
    g_assert(info.size == 64);
    int fd = open(getenv("OPK_DMABUF_TEST_DEST"), O_RDWR);
    g_assert(fd >= 0);
    void *data = mmap(NULL, info.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    g_assert(data != MAP_FAILED);
    GstBuffer *dest = gst_buffer_new_wrapped_full(0, data, info.size, 0,
                                                info.size, NULL, NULL);
    GstBuffer *src = gst_buffer_new_allocate(NULL, info.size, NULL);
    gst_buffer_memset(src, 0, 0xab, info.size);
    GstVideoFrame in, out;
    g_assert(gst_video_frame_map(&in, &info, src, GST_MAP_READ));
    g_assert(gst_video_frame_map(&out, &info, dest, GST_MAP_WRITE));
    if (strcmp(mode, "plane") == 0)
        g_assert(gst_video_frame_copy_plane(&out, &in, 0));
    else
        g_assert(gst_video_frame_copy(&out, &in));
    gst_video_frame_unmap(&out);
    gst_video_frame_unmap(&in);
    gst_buffer_unref(src);
    gst_buffer_unref(dest);
    munmap(data, info.size);
    close(fd);
    return 0;
}
"""


class DmabufNoCopyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="opk-no-copy-test-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        cls.program = cls.directory / "gst-launch-1.0"
        source = cls.directory / "copy.c"
        source.write_text(COPY_PROGRAM, encoding="utf-8")
        flags = shlex.split(
            subprocess.check_output(
                ["pkg-config", "--cflags", "--libs", "gstreamer-video-1.0"],
                text=True,
            )
        )
        subprocess.run(
            ["cc", "-Wall", "-Wextra", "-Werror", str(source), *flags, "-o", str(cls.program)],
            check=True,
        )

    def setUp(self):
        self.destination = self.directory / "destination"
        self.original = b"\x13" * 64
        self.destination.write_bytes(self.original)
        self.environment = os.environ.copy()
        self.environment.pop("LD_PRELOAD", None)
        self.environment["PATH"] = f"{self.directory}:{self.environment['PATH']}"
        self.environment["OPK_DMABUF_TEST_DEST"] = str(self.destination)

    def run_command(self, command):
        return subprocess.run(
            command, env=self.environment, capture_output=True, text=True, timeout=30
        )

    def run_probe(self):
        return self.run_command(["bash", str(PROBE), "unused-camera", "unused-connector"])

    def test_frame_and_plane_copies_stop_before_writing_pixels(self):
        for mode in ("frame", "plane"):
            with self.subTest(mode=mode):
                self.environment["OPK_DMABUF_TEST_MODE"] = mode
                control = self.run_command([str(self.program)])
                self.assertEqual(control.returncode, 0, control.stderr)
                self.assertEqual(self.destination.read_bytes(), b"\xab" * 64)

                self.destination.write_bytes(self.original)
                guarded = self.run_probe()
                self.assertEqual(guarded.returncode, 1, guarded.stderr)
                self.assertIn("CPU frame copy forbidden", guarded.stderr)
                self.assertEqual(self.destination.read_bytes(), self.original)

    def test_no_copy_process_succeeds(self):
        self.environment["OPK_DMABUF_TEST_MODE"] = "none"
        result = self.run_probe()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_invalid_frame_counts_do_not_start_capture(self):
        self.environment["OPK_DMABUF_TEST_MODE"] = "frame"
        for frames in ("0", "-1", "2147483647", "999999999999", "1+1"):
            with self.subTest(frames=frames):
                result = self.run_command(["bash", str(PROBE), "unused", "unused", frames])
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn("Frame count must be", result.stderr)
                self.assertEqual(self.destination.read_bytes(), self.original)

    def test_stream_readback_failure_stops_camera_and_tcp_sink(self):
        pids = self.directory / "stream-pids"
        pids.unlink(missing_ok=True)
        self.environment["OPK_DMABUF_TEST_MODE"] = "hold"
        self.environment["OPK_DMABUF_TEST_PIDS"] = str(pids)
        result = self.run_command(["bash", str(PROBE), "unused", "35", "90", "5000"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("KMS snapshot", result.stderr)
        running = pids.read_text().splitlines()
        self.assertEqual(len(running), 2)
        for pid in running:
            with self.assertRaises(ProcessLookupError):
                os.kill(int(pid), 0)

    def test_invalid_stream_port_does_not_start_capture(self):
        self.environment["OPK_DMABUF_TEST_MODE"] = "frame"
        for port in ("0", "65536", "-1", "1+1"):
            with self.subTest(port=port):
                result = self.run_command(["bash", str(PROBE), "unused", "35", "90", port])
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(self.destination.read_bytes(), self.original)

    def test_failed_guard_build_does_not_start_capture(self):
        with tempfile.TemporaryDirectory(prefix="opk-no-copy-compiler-") as temporary:
            compiler = Path(temporary) / "cc"
            compiler.write_text("#!/bin/sh\nexit 42\n", encoding="utf-8")
            compiler.chmod(0o755)
            self.environment["PATH"] = f"{temporary}:{self.environment['PATH']}"
            self.environment["OPK_DMABUF_TEST_MODE"] = "frame"
            result = self.run_probe()
            self.assertEqual(result.returncode, 42, result.stderr)
            self.assertEqual(self.destination.read_bytes(), self.original)

    def test_guard_that_cannot_load_does_not_start_capture(self):
        with tempfile.TemporaryDirectory(prefix="opk-no-copy-loader-") as temporary:
            compiler = Path(temporary) / "cc"
            compiler.write_text(
                '#!/bin/bash\nprintf "invalid library" > "${!#}"\n', encoding="utf-8"
            )
            compiler.chmod(0o755)
            self.environment["PATH"] = f"{temporary}:{self.environment['PATH']}"
            self.environment["OPK_DMABUF_TEST_MODE"] = "frame"
            result = self.run_probe()
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn("copy guard did not load", result.stderr)
            self.assertEqual(self.destination.read_bytes(), self.original)


HARNESS = r"""
#define main snapshot_main
#include "kms-snapshot.c"
#undef main
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    size_t size = 32 + 704 * 720;
    unsigned char *data = calloc(1, size);
    if (!data)
        return 2;
    drmModeFB2 fb = {.width = 640, .height = 480, .pixel_format = DRM_FORMAT_NV12,
                    .handles = {1, 1}, .pitches = {704, 704},
                    .offsets = {32, 32 + 704 * 480}};
    for (size_t row = 0; row < 720; ++row)
        memset(data + 32 + row * 704, row % 251, 640);
    if (strcmp(argv[1], "truncated") == 0)
        size = fb.offsets[1] + 239 * 704 + 639;
    else if (strcmp(argv[1], "stride") == 0)
        fb.pitches[1] = 639;
    else if (strcmp(argv[1], "tiled") == 0) {
        fb.flags = DRM_MODE_FB_MODIFIERS;
        fb.modifier = 1;
    } else if (strcmp(argv[1], "separate") == 0)
        fb.handles[1] = 2;
    else if (strcmp(argv[1], "dimensions") == 0)
        fb.width = 642;
    else if (strcmp(argv[1], "format") == 0)
        fb.pixel_format = DRM_FORMAT_NV21;
    int result = write_nv12(stdout, data, size, &fb);
    free(data);
    return result ? 1 : 0;
}
"""


class KmsSnapshotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="opk-kms-test-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        source = cls.directory / "layout.c"
        source.write_text(HARNESS, encoding="utf-8")
        cls.program = cls.directory / "layout"
        flags = shlex.split(
            subprocess.check_output(["pkg-config", "--cflags", "--libs", "libdrm"], text=True)
        )
        subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I",
             str(ROOT / "scripts/private"), str(source), *flags, "-o", str(cls.program)],
            check=True,
        )

    def test_padded_planes_are_packed_without_padding_or_offset_bytes(self):
        result = subprocess.run([str(self.program), "valid"], capture_output=True, check=True)
        expected = b"".join(bytes([row % 251]) * 640 for row in range(720))
        self.assertEqual(result.stdout, expected)

    def test_unsupported_or_out_of_bounds_layout_writes_no_pixels(self):
        for mode in ("truncated", "stride", "tiled", "separate", "dimensions", "format"):
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.program), mode], capture_output=True)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, b"")


if __name__ == "__main__":
    unittest.main()
