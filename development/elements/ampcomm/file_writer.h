#pragma once

#include <mutex>

#include <sys/stat.h>

#include "writer.h"

class FileWriter : public Writer {
    std::string m_file_name;

    int m_fd = -1;

    std::recursive_mutex m_io_lock;

    bool io_open_existing(struct stat &st);
    bool io_create();

  protected:
    bool io_open() override;
    void io_close() override;

    bool publish(const std::string &json_str) override;

  public:
    explicit FileWriter(_GstAmpComm *self, const std::string &file_name, size_t queue_size)
        : Writer(self, queue_size), m_file_name(file_name) {}
};
