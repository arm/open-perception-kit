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
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

struct param {
    uint32_t param, pad;
    uint64_t value;
};
static atomic_int mode, skip;
static _Atomic uint64_t offset;
void reset_arm(int kind, int delay) {
    skip = delay;
    mode = kind;
}

int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    void *arg = va_arg(args, void *);
    va_end(args);
    const int result = syscall(SYS_ioctl, fd, request, arg);
    Dl_info caller;
    if (request != _IOWR('d', 0x44, struct param) || !arg || ((struct param *)arg)->param != 15 ||
        !dladdr(__builtin_return_address(0), &caller) ||
        !strstr(caller.dli_fname, "opk-mlwl-ops.so")) {
        return result;
    }
    if (result < 0) {
        return result;
    }
    if (mode && skip-- == 0) {
        const int failure = mode == 2;
        mode = 0;
        if (failure) {
            errno = EIO;
            return -1;
        }
        ++offset;
    }
    ((struct param *)arg)->value += offset;
    return result;
}
