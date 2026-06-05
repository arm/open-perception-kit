/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#ifndef __HTTP_SERVER_H__
#define __HTTP_SERVER_H__

#include <httplib.h>

#include <thread>

enum class PekSinkHttpServerError {
    OK,
    NO_STATIC_FILES_DIRECTORY,
    CANNOT_BIND_SERVER_PORT,
};

struct _GstPekSink;

class PekSinkHttpServer {
    _GstPekSink *self_ = nullptr;

    std::unique_ptr<httplib::Server> http_server = nullptr;

    std::thread http_server_thread;

    void get_dynamic_config(const httplib::Request &req, httplib::Response &res);
    void get_model_info(const httplib::Request &req, httplib::Response &res);
    void get_pipelines(const httplib::Request &req, httplib::Response &res);

    bool listen(const std::string &host, int port) {
        return http_server->listen(host, port);
    }

    PekSinkHttpServerError setup();

  public:
    PekSinkHttpServer() = default;
    PekSinkHttpServer(_GstPekSink *self) : self_(self) {};

    PekSinkHttpServerError start();
    PekSinkHttpServerError stop();
};

#endif // !__HTTP_SERVER_H__
