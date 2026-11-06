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
#include <stdexcept>
#include <unistd.h>

#include "file_writer.h"

class FileError : public std::runtime_error {
  public:
    explicit FileError(const std::string &message) : std::runtime_error(message) {}
};

bool FileWriter::io_open_existing(const struct stat &fileStatus) {
    if (S_ISFIFO(fileStatus.st_mode)) {
        m_fd = open(m_file_name.c_str(), O_WRONLY | O_NONBLOCK);
        if (m_fd < 0) {
            /* ENXIO is normal when no reader is connected yet */
            if (errno == ENXIO) {
                GST_INFO_OBJECT(self(), "FIFO '%s' has no reader yet", m_file_name.c_str());
            } else {
                GST_INFO_OBJECT(
                    self(), "Failed to open FIFO '%s': %s", m_file_name.c_str(), g_strerror(errno));
            }

            throw FileError("FIFO cannot be opened");
        }
        return true;

    } else if (S_ISREG(fileStatus.st_mode)) {
        m_fd = open(m_file_name.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0640);
        if (m_fd < 0) {
            GST_INFO_OBJECT(
                self(), "Failed to open file '%s': %s", m_file_name.c_str(), g_strerror(errno));

            throw FileError("File cannot be opened");
        }

        return true;

    } else {
        GST_WARNING_OBJECT(
            self(), "'%s' exists but is not a FIFO or regular file", m_file_name.c_str());

        throw FileError("Unknown file type");
    }
}

bool FileWriter::io_create() {
    if (errno != ENOENT) {
        GST_INFO_OBJECT(self(), "stat('%s') failed: %s", m_file_name.c_str(), g_strerror(errno));

        throw FileError("Cannot stat");
    }

    /* Doesn't exist -> create as regular file */
    m_fd = open(m_file_name.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0640);
    if (m_fd < 0) {
        GST_INFO_OBJECT(
            self(), "Failed to create file '%s': %s", m_file_name.c_str(), g_strerror(errno));

        throw FileError("File cannot be created");
    }

    return true;
}

bool FileWriter::io_open() {
    std::scoped_lock lock(m_io_lock);

    try {
        auto success = false;
        if (m_file_name.empty()) {
            GST_WARNING_OBJECT(self(), "file-name is not set");

            throw FileError("file-name is not set");
        }

        /* "-" means stdout */
        if ("-" == m_file_name) {
            m_fd = STDOUT_FILENO;
            success = true;
        } else {

            // it is not stdout

            struct stat fileStatus;
            if (stat(m_file_name.c_str(), &fileStatus) == 0) {
                /* Path exists: decide by type */
                success = io_open_existing(fileStatus);

            } else {
                /* Path doesn't exist or stat failed */
                success = io_create();
            }
        }

        return success;

    } catch (FileError &) {
        return false;
    }
}

void FileWriter::io_close() {
    std::scoped_lock lock(m_io_lock);

    if (m_fd == STDOUT_FILENO) {
        m_fd = -1;
    }

    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
}

int FileWriter::check_open() {
    int descriptor = -1;

    std::scoped_lock lock(m_io_lock);

    if (m_fd < 0)
        io_open();
    if (m_fd >= 0)
        descriptor = dup(m_fd);

    return descriptor;
}

bool FileWriter::publish(const std::string &record) {
    const int descriptor = check_open();
    if (descriptor < 0)
        return false;
    size_t offset = 0;
    while (running() && offset < record.size()) {
        const auto bytesWritten =
            ::write(descriptor, record.data() + offset, record.size() - offset);
        if (bytesWritten > 0) {
            offset += static_cast<size_t>(bytesWritten);
        } else if (bytesWritten < 0 && errno == EINTR) {
            continue;
        } else if (bytesWritten < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            std::this_thread::sleep_for(retryInterval);
        } else {
            break;
        }
    }
    close(descriptor);
    return offset == record.size();
}
