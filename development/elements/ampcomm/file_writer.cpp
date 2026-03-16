#include <glib.h>
/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <gst/gst.h>

#include <errno.h>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

#include "file_writer.h"

static bool write_all(int fd, const char *data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::write(fd, data + off, len - off);
        if (n > 0) {
            off += size_t(n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;

        // EAGAIN = would block (nonblocking FIFO). Your policy: drop.
        return false;
    }
    return true;
}

bool FileWriter::io_open_existing(struct stat &st) {
    if (S_ISFIFO(st.st_mode)) {
        m_fd = open(m_file_name.c_str(), O_WRONLY | O_NONBLOCK);
        if (m_fd < 0) {
            /* ENXIO is normal when no reader is connected yet */
            if (errno == ENXIO) {
                GST_INFO_OBJECT(self(), "FIFO '%s' has no reader yet", m_file_name.c_str());
            } else {
                GST_INFO_OBJECT(
                    self(), "Failed to open FIFO '%s': %s", m_file_name.c_str(), g_strerror(errno));
            }

            throw std::runtime_error("FIFO cannot be opened");
        }
        return true;

    } else if (S_ISREG(st.st_mode)) {
        m_fd = open(m_file_name.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (m_fd < 0) {
            GST_INFO_OBJECT(
                self(), "Failed to open file '%s': %s", m_file_name.c_str(), g_strerror(errno));

            throw std::runtime_error("File cannot be opened");
        }

        return true;

    } else {
        GST_WARNING_OBJECT(
            self(), "'%s' exists but is not a FIFO or regular file", m_file_name.c_str());

        throw std::runtime_error("Unknown file type");
    }
}

bool FileWriter::io_create() {
    if (errno != ENOENT) {
        GST_INFO_OBJECT(self(), "stat('%s') failed: %s", m_file_name.c_str(), g_strerror(errno));

        throw std::runtime_error("Cannot stat");
    }

    /* Doesn't exist -> create as regular file */
    m_fd = open(m_file_name.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (m_fd < 0) {
        GST_INFO_OBJECT(
            self(), "Failed to create file '%s': %s", m_file_name.c_str(), g_strerror(errno));

        throw std::runtime_error("File cannot be created");
    }

    return true;
}

bool FileWriter::io_open() {
    std::lock_guard g(m_io_lock);

    try {
        auto ret = false;
        if (m_file_name.empty()) {
            GST_WARNING_OBJECT(self(), "file-name is not set");

            throw std::runtime_error("file-name is not set");
        }

        /* "-" means stdout */
        if (g_strcmp0("-", m_file_name.c_str()) == 0) {
            m_fd = STDOUT_FILENO;
            ret = true;
        } else {

            // it is not stdout

            struct stat st;
            if (stat(m_file_name.c_str(), &st) == 0) {
                /* Path exists: decide by type */
                ret = io_open_existing(st);

            } else {
                /* Path doesn't exist or stat failed */
                ret = io_create();
            }
        }

        return ret;

    } catch (std::runtime_error &) {
        return false;
    }
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

bool FileWriter::publish(const std::string &json_str) {
    int fd = -1;

    {
        std::lock_guard g(m_io_lock);

        if (m_fd < 0)
            io_open();
        if (m_fd >= 0)
            fd = dup(m_fd);
    }

    bool ret = false;
    if (fd >= 0) {
        ssize_t n = write(fd, json_str.c_str(), json_str.length());
        close(fd);

        if (n < 0) {
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
