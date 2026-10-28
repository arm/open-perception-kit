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
