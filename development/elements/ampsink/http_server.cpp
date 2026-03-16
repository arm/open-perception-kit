/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <format>
#include <string>

#include <nlohmann/json.hpp>

// WebRTC in GST is unstable: this macro disables the warning
#define GST_USE_UNSTABLE_API

#include "ampsink.h"
#include "http_server.h"
#include "nlohmann/json_fwd.hpp"

using namespace httplib;
using namespace nlohmann;

AmpSinkHttpServerError AmpSinkHttpServer::setup() {

    http_server = std::make_unique<Server>();

    // Dynamic config endpoint
    http_server->Get("/amp-config.js",
                     [this](const Request &req, Response &res) { get_dynamic_config(req, res); });

    auto ret = http_server->set_mount_point("/", self_->static_files_location);
    if (!ret) {
        // TODO@ibori: error handling
        auto sfl = self_->static_files_location ? self_->static_files_location : "(null)";
        std::cerr << std::format("Static file directory does not exist: {}", sfl);

        return AmpSinkHttpServerError::NO_STATIC_FILES_DIRECTORY;
    }

    return AmpSinkHttpServerError::OK;
}

AmpSinkHttpServerError AmpSinkHttpServer::start() {
    if (auto error = setup(); error != AmpSinkHttpServerError::OK) {
        return error;
    }

    http_server_thread =
        std::thread(&AmpSinkHttpServer::listen, this, self_->host, self_->http_port);
    http_server->wait_until_ready();
    if (http_server->is_running()) {
        return AmpSinkHttpServerError::OK;
    } else {
        const auto host = std::string(self_->host ? self_->host : "<null>");
        std::cerr << std::format("HTTP server failed to start on {}:{}\n", host, self_->http_port);

        return AmpSinkHttpServerError::CANNOT_BIND_SERVER_PORT;
    }
}

AmpSinkHttpServerError AmpSinkHttpServer::stop() {

    if (http_server) {
        http_server->stop();
    }

    if (http_server_thread.joinable()) {
        http_server_thread.join();
    }

    return AmpSinkHttpServerError::OK;
}

void AmpSinkHttpServer::get_dynamic_config(const Request &req, Response &res) {
    std::string js = "window.AMP_CONFIG = "
                     "{ wsPort: " +
                     std::to_string(self_->ws_port) + "," +
                     "ctrlPort: " + std::to_string(self_->ctrl_port) + "};";
    res.set_content(js, "application/javascript");
}
