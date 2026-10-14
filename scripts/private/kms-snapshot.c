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

#define _POSIX_C_SOURCE 200809L
#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

// shortcut: only shared linear NV12 640x480; extend when the probe's camera mode changes.
static int write_nv12(FILE *output, const unsigned char *data, size_t size, const drmModeFB2 *fb) {
    if (fb->pixel_format != DRM_FORMAT_NV12 || fb->width != 640 || fb->height != 480 ||
        ((fb->flags & DRM_MODE_FB_MODIFIERS) && fb->modifier != DRM_FORMAT_MOD_LINEAR) ||
        !fb->handles[0] || fb->handles[0] != fb->handles[1] || fb->handles[2] || fb->handles[3]) {
        fprintf(stderr, "Expected the probe's linear NV12 640x480 shared DMA buffer.\n");
        return -1;
    }
    for (unsigned int plane = 0; plane < 2; ++plane) {
        const size_t rows = plane == 0 ? fb->height : fb->height / 2;
        const uint64_t end =
            (uint64_t)fb->offsets[plane] + (rows - 1) * (uint64_t)fb->pitches[plane] + fb->width;
        if (fb->pitches[plane] < fb->width || end > size) {
            fprintf(stderr, "Plane layout exceeds the DMA buffer allocation.\n");
            return -1;
        }
    }
    for (unsigned int plane = 0; plane < 2; ++plane) {
        const size_t rows = plane == 0 ? fb->height : fb->height / 2;
        for (size_t row = 0; row < rows; ++row) {
            const size_t offset = fb->offsets[plane] + row * fb->pitches[plane];
            if (fwrite(data + offset, 1, fb->width, output) != fb->width)
                return -1;
        }
    }
    return fflush(output);
}

static drmModePlane *camera_plane(int drm, uint32_t connector_id) {
    drmModeConnector *connector = drmModeGetConnector(drm, connector_id);
    if (!connector)
        return NULL;
    drmModeEncoder *encoder = drmModeGetEncoder(drm, connector->encoder_id);
    drmModeFreeConnector(connector);
    if (!encoder)
        return NULL;
    const uint32_t crtc = encoder->crtc_id;
    drmModeFreeEncoder(encoder);
    drmModePlaneRes *planes = drmModeGetPlaneResources(drm);
    if (!planes)
        return NULL;
    drmModePlane *selected = NULL;
    for (uint32_t i = 0; i < planes->count_planes; ++i) {
        drmModePlane *plane = drmModeGetPlane(drm, planes->planes[i]);
        if (plane && crtc && plane->crtc_id == crtc && plane->fb_id) {
            drmModeFB2 *fb = drmModeGetFB2(drm, plane->fb_id);
            const int camera =
                fb && fb->pixel_format == DRM_FORMAT_NV12 && fb->width == 640 && fb->height == 480;
            drmModeFreeFB2(fb);
            if (camera) {
                selected = plane;
                break;
            }
        }
        drmModeFreePlane(plane);
    }
    drmModeFreePlaneResources(planes);
    return selected;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s CONNECTOR_ID OUTPUT.nv12\n", argv[0]);
        return EXIT_FAILURE;
    }
    char *end = NULL;
    errno = 0;
    unsigned long connector_id = strtoul(argv[1], &end, 10);
    if (errno || end == argv[1] || *end || !connector_id || connector_id > UINT32_MAX) {
        fprintf(stderr, "Invalid connector ID.\n");
        return EXIT_FAILURE;
    }
    int result = EXIT_FAILURE;
    int drm = -1, dma = -1;
    drmModePlane *plane = NULL;
    drmModeFB2 *fb = NULL;
    unsigned char *data = MAP_FAILED;
    off_t size = 0;
    FILE *output = NULL;
    int sync_started = 0;
    struct dma_buf_sync sync = {.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ};

    drm = drmOpen("vc4", NULL);
    if (drm < 0 || drmSetClientCap(drm, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1))
        goto done;
    plane = camera_plane(drm, (uint32_t)connector_id);
    if (!plane) {
        fprintf(stderr, "Waiting for the connector's NV12 camera framebuffer.\n");
        result = 3;
        goto done;
    }
    fb = drmModeGetFB2(drm, plane->fb_id);
    if (!fb) {
        result = 3;
        goto done;
    }
    if (!fb->handles[0]) {
        fprintf(stderr, "Cannot read framebuffer handles; root/CAP_SYS_ADMIN is required.\n");
        goto done;
    }
    if (drmPrimeHandleToFD(drm, fb->handles[0], DRM_CLOEXEC, &dma))
        goto done;
    size = lseek(dma, 0, SEEK_END);
    if (size <= 0)
        goto done;
    data = mmap(NULL, (size_t)size, PROT_READ, MAP_SHARED, dma, 0);
    if (data == MAP_FAILED || ioctl(dma, DMA_BUF_IOCTL_SYNC, &sync))
        goto done;
    sync_started = 1;
    output = fopen(argv[2], "wbx");
    if (!output || write_nv12(output, data, (size_t)size, fb))
        goto done;
    fprintf(stderr,
            "Read plane %u, CRTC %u, framebuffer %u; DMA allocation %jd bytes, "
            "NV12 offsets %u/%u, pitches %u/%u.\n",
            plane->plane_id,
            plane->crtc_id,
            plane->fb_id,
            (intmax_t)size,
            fb->offsets[0],
            fb->offsets[1],
            fb->pitches[0],
            fb->pitches[1]);
    result = EXIT_SUCCESS;
done:
    if (result != EXIT_SUCCESS && errno)
        perror("KMS snapshot");
    if (sync_started) {
        sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
        if (ioctl(dma, DMA_BUF_IOCTL_SYNC, &sync)) {
            perror("DMA buffer read synchronization");
            result = EXIT_FAILURE;
        }
    }
    if (output && fclose(output))
        result = EXIT_FAILURE;
    if (data != MAP_FAILED)
        munmap(data, (size_t)size);
    if (dma >= 0)
        close(dma);
    drmModeFreeFB2(fb);
    drmModeFreePlane(plane);
    if (drm >= 0)
        close(drm);
    return result;
}
