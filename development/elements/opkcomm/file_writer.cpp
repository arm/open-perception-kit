#include <glib.h>
/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#include <gst/gst.h>

#include <errno.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <linux/openat2.h>
#include <span>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "file_writer.h"

constexpr int kWriteFlags = O_WRONLY | O_APPEND | O_NONBLOCK | O_CLOEXEC;

static int open_existing_output(int directory_fd, int path_fd) {
    struct stat st;
    if (fstat(path_fd, &st) != 0)
        return -1;
    if (!S_ISREG(st.st_mode) && !S_ISFIFO(st.st_mode)) {
        errno = EINVAL;
        return -1;
    }

    if (S_ISREG(st.st_mode)) {
        struct stat directory;
        if (fstat(directory_fd, &directory) != 0)
            return -1;
        // Reopening through procfs skips the kernel's sticky-directory O_CREAT check.
        if ((directory.st_mode & S_ISVTX) && (directory.st_mode & (S_IWOTH | S_IWGRP))) {
            int protected_regular = 0;
            uid_t overflow_uid = 0;
            std::ifstream setting("/proc/sys/fs/protected_regular");
            std::ifstream overflow_setting("/proc/sys/kernel/overflowuid");
            const bool have_settings =
                (setting >> protected_regular) && (overflow_setting >> overflow_uid);
            const bool protected_directory =
                protected_regular > 0 && ((directory.st_mode & S_IWOTH) ||
                                          (protected_regular > 1 && (directory.st_mode & S_IWGRP)));
            // Unmapped owners can share the overflow UID; it cannot establish ownership.
            const bool allowed_owner = st.st_uid != overflow_uid &&
                                       (st.st_uid == directory.st_uid ||
                                        st.st_uid == static_cast<uid_t>(syscall(SYS_setfsuid, -1)));
            if (!have_settings || (protected_directory && !allowed_owner)) {
                errno = EACCES;
                return -1;
            }
        }
    }

    // Use the validated inode even if the configured path is replaced.
    return open(("/proc/self/fd/" + std::to_string(path_fd)).c_str(), kWriteFlags);
}

static bool write_all(int fd, const std::span<const char> data) {
    size_t off = 0;

    const auto d = data.data();
    const auto len = data.size();

    while (off < len) {
        ssize_t n = ::write(fd, d + off, len - off);
        if (n > 0) {
            off += size_t(n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;

        // EAGAIN = would block (nonblocking FIFO).
        return false;
    }
    return true;
}

bool FileWriter::io_open() {
    std::scoped_lock g(m_io_lock);

    if (m_file_name.empty()) {
        GST_WARNING_OBJECT(self(), "file-name is not set");
        return false;
    }

    /* "-" means stdout */
    if ("-" == m_file_name) {
        m_fd = STDOUT_FILENO;
        return true;
    }

    const std::filesystem::path path(m_file_name);
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    const auto name = path.filename();
    struct open_how how{
        .flags = O_PATH | O_DIRECTORY | O_CLOEXEC,
        .mode = 0,
        .resolve = RESOLVE_NO_SYMLINKS,
    };
    const int directory_fd =
        static_cast<int>(syscall(SYS_openat2, AT_FDCWD, parent.c_str(), &how, sizeof(how)));
    int fd = -1;
    if (directory_fd >= 0) {
        how.flags = O_PATH | O_CLOEXEC;
        int path_fd =
            static_cast<int>(syscall(SYS_openat2, directory_fd, name.c_str(), &how, sizeof(how)));
        if (path_fd < 0 && errno == ENOENT) {
            how.flags = kWriteFlags | O_CREAT | O_EXCL;
            how.mode = 0640;
            fd = static_cast<int>(
                syscall(SYS_openat2, directory_fd, name.c_str(), &how, sizeof(how)));
            if (fd < 0 && errno == EEXIST) {
                // A competing creator may have won the exclusive create.
                how.flags = O_PATH | O_CLOEXEC;
                how.mode = 0;
                path_fd = static_cast<int>(
                    syscall(SYS_openat2, directory_fd, name.c_str(), &how, sizeof(how)));
            }
        }
        if (path_fd >= 0)
            fd = open_existing_output(directory_fd, path_fd);
        const int saved_errno = errno;
        if (path_fd >= 0)
            close(path_fd);
        close(directory_fd);
        errno = saved_errno;
    }
    if (fd < 0) {
        if (errno == ENXIO) {
            GST_INFO_OBJECT(self(), "FIFO '%s' has no reader yet", m_file_name.c_str());
        } else {
            GST_INFO_OBJECT(
                self(), "Failed to open file '%s': %s", m_file_name.c_str(), g_strerror(errno));
        }
        return false;
    }

    m_fd = fd;
    return true;
}

void FileWriter::io_close() {
    std::lock_guard g(m_io_lock);

    if (m_fd == STDOUT_FILENO) {
        m_fd = -1;
    }

    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
}

int FileWriter::check_open() {
    int fd = -1;

    std::lock_guard g(m_io_lock);

    if (m_fd < 0)
        io_open();
    if (m_fd >= 0)
        fd = fcntl(m_fd, F_DUPFD_CLOEXEC, 0);

    return fd;
}

bool FileWriter::publish(const std::string &json_str) {
    bool ret = false;

    if (auto fd = check_open(); fd >= 0) {
        bool ok = write_all(fd, std::span<const char>(json_str.c_str(), json_str.size()));
        close(fd);

        if (!ok) {
            if (errno == EPIPE || errno == ENXIO) {
                GST_INFO_OBJECT(self(), "FIFO reader disappeared for '%s'", m_file_name.c_str());
                io_close();
            } else if (errno == EAGAIN) {
                /* FIFO full -> drop */
            } else {
                GST_DEBUG_OBJECT(self(), "FIFO write error: %s", g_strerror(errno));
            }
        } else {
            ret = true;
        }
    }

    return ret;
}
