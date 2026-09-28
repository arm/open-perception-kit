/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#ifndef __HTTP_SERVER_H__
#define __HTTP_SERVER_H__

#include <httplib.h>

#include <thread>

enum class OpkSinkHttpServerError {
    OK,
    NO_STATIC_FILES_DIRECTORY,
    CANNOT_BIND_SERVER_PORT,
};

struct _GstOpkSink;

class OpkSinkHttpServer {
    _GstOpkSink *self_ = nullptr;

    std::unique_ptr<httplib::Server> http_server = nullptr;

    std::thread http_server_thread;

    void get_dynamic_config(const httplib::Request &req, httplib::Response &res);
    void get_model_info(const httplib::Request &req, httplib::Response &res);

    bool listen(const std::string &host, int port) {
        return http_server->listen(host, port);
    }

    OpkSinkHttpServerError setup();

  public:
    OpkSinkHttpServer() = default;
    OpkSinkHttpServer(_GstOpkSink *self) : self_(self) {};

    OpkSinkHttpServerError start();
    OpkSinkHttpServerError stop();
};

#endif // !__HTTP_SERVER_H__
