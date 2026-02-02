#ifndef __HTTP_SERVER_H__
#define __HTTP_SERVER_H__

#include <httplib.h>

#include <thread>

enum class AmpSinkHttpServerError {
    OK,
    NO_STATIC_FILES_DIRECTORY,
    CANNOT_BIND_SERVER_PORT,
};

struct _GstAmpSink;

class AmpSinkHttpServer {
    _GstAmpSink *self_ = nullptr;

    std::unique_ptr<httplib::Server> http_server = nullptr;

    std::thread http_server_thread;

    void get_dynamic_config(const httplib::Request &req, httplib::Response &res);

    bool listen(const std::string &host, int port) {
        return http_server->listen(host, port);
    }

    AmpSinkHttpServerError setup();

  public:
    AmpSinkHttpServer() = default;
    AmpSinkHttpServer(_GstAmpSink *self) : self_(self) {};

    AmpSinkHttpServerError start();
    AmpSinkHttpServerError stop();
};

#endif // !__HTTP_SERVER_H__
