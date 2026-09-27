#include <glib.h>
/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <gst/gst.h>

#include <errno.h>
#include <fcntl.h>
#include <span>
#include <sys/stat.h>
#include <unistd.h>

#include "file_writer.h"

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
    std::lock_guard g(m_io_lock);

    if (m_file_name.empty()) {
        GST_WARNING_OBJECT(self(), "file-name is not set");
        return false;
    }

    /* "-" means stdout */
    if ("-" == m_file_name) {
        m_fd = STDOUT_FILENO;
        return true;
    }

    constexpr int flags = O_WRONLY | O_APPEND | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC | O_CREAT;
    int fd = open(m_file_name.c_str(), flags, 0640);
    if (fd < 0) {
        if (errno == ENXIO) {
            GST_INFO_OBJECT(self(), "FIFO '%s' has no reader yet", m_file_name.c_str());
        } else {
            GST_INFO_OBJECT(
                self(), "Failed to open file '%s': %s", m_file_name.c_str(), g_strerror(errno));
        }
        return false;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        const int saved_errno = errno;
        close(fd);
        GST_WARNING_OBJECT(self(),
                           "Failed to inspect file '%s': %s",
                           m_file_name.c_str(),
                           g_strerror(saved_errno));
        return false;
    }
    if (!S_ISFIFO(st.st_mode) && !S_ISREG(st.st_mode)) {
        close(fd);
        GST_WARNING_OBJECT(self(), "'%s' is not a FIFO or regular file", m_file_name.c_str());
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
