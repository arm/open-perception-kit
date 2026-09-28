/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include <mutex>

#include <sys/stat.h>

#include "writer.h"

class FileWriter : public Writer {
    std::string m_file_name;

    int m_fd = -1;

    std::recursive_mutex m_io_lock;

    int check_open();

    bool io_open_existing(const struct stat &st);
    bool io_create();

  protected:
    bool io_open() override;
    void io_close() override;

    bool publish(const std::string &json_str) override;

  public:
    explicit FileWriter(_GstOpkComm *self, const std::string &file_name, size_t queue_size)
        : Writer(self, queue_size), m_file_name(file_name) {}

    ~FileWriter() override = default;
};
