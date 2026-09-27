/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <string>

#include <nlohmann/json.hpp>

// WebRTC in GST is unstable: this macro disables the warning
#define GST_USE_UNSTABLE_API

#include "Log.h"
#include "http_server.h"
#include "nlohmann/json_fwd.hpp"
#include "opksink.h"

using namespace httplib;
using namespace nlohmann;

static std::string without_webrtc_url_slashes(std::string url) {
    const auto scheme_end = url.find("://");
    if (scheme_end != std::string::npos) {
        url.erase(scheme_end + 1, 2);
    }
    return url;
}

static json browser_ice_server_from_url(const char *server_url) {
    if (!server_url || server_url[0] == '\0') {
        return nullptr;
    }

    std::string url = without_webrtc_url_slashes(server_url);
    json ice_server;

    const auto scheme_end = url.find(':');
    const auto at = url.find('@');
    if (scheme_end != std::string::npos && at != std::string::npos && at > scheme_end + 1) {
        const auto credentials = url.substr(scheme_end + 1, at - scheme_end - 1);
        const auto colon = credentials.find(':');
        if (colon != std::string::npos) {
            const auto username_enc = credentials.substr(0, colon);
            const auto credential_enc = credentials.substr(colon + 1);

            gchar *username = g_uri_unescape_string(username_enc.c_str(), nullptr);
            gchar *credential = g_uri_unescape_string(credential_enc.c_str(), nullptr);

            ice_server["username"] = username ? username : username_enc;
            ice_server["credential"] = credential ? credential : credential_enc;

            g_free(username);
            g_free(credential);

            url.erase(scheme_end + 1, at - scheme_end);
        }
    }

    ice_server["urls"] = url;
    return ice_server;
}

OpkSinkHttpServerError OpkSinkHttpServer::setup() {

    http_server = std::make_unique<Server>();
    // Override httplib's SO_REUSEPORT default to prevent sinks sharing an HTTP port.
    http_server->set_socket_options(
        [](socket_t sock) { set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1); });

    // Dynamic config endpoint
    http_server->Get("/opk-config.js",
                     [this](const Request &req, Response &res) { get_dynamic_config(req, res); });

    auto ret = http_server->set_mount_point("/", self_->static_files_location);
    if (!ret) {
        // TODO@ibori: error handling
        auto sfl = self_->static_files_location ? self_->static_files_location : "(null)";
        opk::log::error("Static file directory does not exist: {}\n", sfl);

        return OpkSinkHttpServerError::NO_STATIC_FILES_DIRECTORY;
    }

    return OpkSinkHttpServerError::OK;
}

OpkSinkHttpServerError OpkSinkHttpServer::start() {
    if (!self_->host) {
        opk::log::error("HTTP server host must not be null\n");
        return OpkSinkHttpServerError::CANNOT_BIND_SERVER_PORT;
    }

    if (auto error = setup(); error != OpkSinkHttpServerError::OK) {
        return error;
    }

    http_server_thread =
        std::thread(&OpkSinkHttpServer::listen, this, self_->host, self_->http_port);
    http_server->wait_until_ready();
    if (http_server->is_running()) {
        return OpkSinkHttpServerError::OK;
    } else {
        const auto host = std::string(self_->host ? self_->host : "<null>");
        opk::log::error("HTTP server failed to start on {}:{}\n", host, self_->http_port);

        return OpkSinkHttpServerError::CANNOT_BIND_SERVER_PORT;
    }
}

OpkSinkHttpServerError OpkSinkHttpServer::stop() {

    if (http_server) {
        http_server->stop();
    }

    if (http_server_thread.joinable()) {
        http_server_thread.join();
    }
    http_server.reset();

    return OpkSinkHttpServerError::OK;
}

void OpkSinkHttpServer::get_dynamic_config(const Request &req, Response &res) {
    json config = {
        {"wsPort", self_->ws_port},
        {"ctrlPort", self_->ctrl_port},
    };

    json ice_servers = json::array();
    if (auto stun_server = browser_ice_server_from_url(self_->webrtc_stun_server);
        !stun_server.is_null()) {
        ice_servers.push_back(stun_server);
    }
    if (auto turn_server = browser_ice_server_from_url(self_->webrtc_turn_server);
        !turn_server.is_null()) {
        ice_servers.push_back(turn_server);
    }
    if (!ice_servers.empty()) {
        config["webrtc"]["iceServers"] = ice_servers;
    }

    std::string js = "window.OPK_CONFIG = " + config.dump() + ";";
    res.set_content(js, "application/javascript");
}
