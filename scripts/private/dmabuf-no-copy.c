/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gst/video/video.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// A loader failure must stop the probe instead of silently ignoring LD_PRELOAD.
__attribute__((constructor)) static void check_guard(void) {
    const char *check = getenv("OPK_DMABUF_GUARD_CHECK");
    if (check && strcmp(check, "1") == 0) {
        _exit(77);
    }
}

// shortcut: covers the tested source/sink copy APIs; recheck after plugin upgrades.
static _Noreturn void reject_copy(void) {
    static const char message[] =
        "DMA-BUF probe: CPU frame copy forbidden; stopping before copying.\n";
    (void)write(STDERR_FILENO, message, sizeof(message) - 1);
    _exit(EXIT_FAILURE);
}

gboolean gst_video_frame_copy(GstVideoFrame *dest, const GstVideoFrame *src) {
    (void)dest;
    (void)src;
    reject_copy();
}

gboolean gst_video_frame_copy_plane(GstVideoFrame *dest, const GstVideoFrame *src, guint plane) {
    (void)dest;
    (void)src;
    (void)plane;
    reject_copy();
}
