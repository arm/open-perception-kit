/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <format>
#include <string>

#include <nlohmann/json.hpp>

// WebRTC in GST is unstable: this macro disables the warning
#define GST_USE_UNSTABLE_API

#include "http_server.h"
#include "nlohmann/json_fwd.hpp"
#include "peksink.h"
#include <filesystem>
#include <fstream>

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

PekSinkHttpServerError PekSinkHttpServer::setup() {

    http_server = std::make_unique<Server>();

    // Dynamic config endpoint
    http_server->Get("/pek-config.js",
                     [this](const Request &req, Response &res) { get_dynamic_config(req, res); });

    // API to return model.json for a named model. The handler will search
    // common config locations for a matching model directory or a model.json
    // whose `name` field matches the requested name.
    http_server->Get("/api/model-info",
                     [this](const Request &req, Response &res) { get_model_info(req, res); });

    auto ret = http_server->set_mount_point("/", self_->static_files_location);
    if (!ret) {
        // TODO@ibori: error handling
        auto sfl = self_->static_files_location ? self_->static_files_location : "(null)";
        std::cerr << std::format("Static file directory does not exist: {}", sfl);

        return PekSinkHttpServerError::NO_STATIC_FILES_DIRECTORY;
    }

    return PekSinkHttpServerError::OK;
}

PekSinkHttpServerError PekSinkHttpServer::start() {
    if (auto error = setup(); error != PekSinkHttpServerError::OK) {
        return error;
    }

    http_server_thread =
        std::thread(&PekSinkHttpServer::listen, this, self_->host, self_->http_port);
    http_server->wait_until_ready();
    if (http_server->is_running()) {
        return PekSinkHttpServerError::OK;
    } else {
        const auto host = std::string(self_->host ? self_->host : "<null>");
        std::cerr << std::format("HTTP server failed to start on {}:{}\n", host, self_->http_port);

        return PekSinkHttpServerError::CANNOT_BIND_SERVER_PORT;
    }
}

PekSinkHttpServerError PekSinkHttpServer::stop() {

    if (http_server) {
        http_server->stop();
    }

    if (http_server_thread.joinable()) {
        http_server_thread.join();
    }

    return PekSinkHttpServerError::OK;
}

void PekSinkHttpServer::get_dynamic_config(const Request &req, Response &res) {
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

    std::string js = "window.PEK_CONFIG = " + config.dump() + ";";
    res.set_content(js, "application/javascript");
}

void PekSinkHttpServer::get_model_info(const Request &req, Response &res) {
    std::string name;
    if (req.has_param("name")) {
        name = req.get_param_value("name");
    }
    nlohmann::json out;

    if (name.empty()) {
        out["error"] = "missing 'name' query parameter";
        res.set_content(out.dump(), "application/json");
        return;
    }

    // Candidate directories to search for model.json and opchain.json
    std::vector<std::filesystem::path> model_bases = {
        std::filesystem::current_path() / "config" / "models",
        std::filesystem::current_path() / ".." / "config" / "models",
        std::filesystem::path("/work") / "config" / "models",
    };

    std::vector<std::filesystem::path> opchain_bases = {
        std::filesystem::current_path() / "config" / "opchains",
        std::filesystem::current_path() / ".." / "config" / "opchains",
        std::filesystem::path("/work") / "config" / "opchains",
    };

    bool found = false;
    for (const auto &base : model_bases) {
        if (!std::filesystem::exists(base) || !std::filesystem::is_directory(base))
            continue;

        // 1) try directory named as `name` for model.json
        std::filesystem::path p1 = base / name;
        std::filesystem::path jpath = p1 / "model.json";
        if (std::filesystem::exists(jpath)) {
            std::ifstream ifs(jpath);
            try {
                nlohmann::json j = nlohmann::json::parse(ifs);
                out = j;
                // continue searching for opchain/opchain description later
                found = true;
                // don't break; we want to allow opchain discovery below
            } catch (...) {
                // parse error, continue
            }
        }

        // 2) scan all model.json files looking for matching `name` field
        for (auto &entry : std::filesystem::directory_iterator(base)) {
            if (!entry.is_directory())
                continue;
            std::filesystem::path mj = entry.path() / "model.json";
            if (!std::filesystem::exists(mj))
                continue;
            std::ifstream ifs(mj);
            try {
                nlohmann::json j = nlohmann::json::parse(ifs);
                if (j.contains("name") && j["name"].is_string() &&
                    j["name"].get<std::string>() == name) {
                    out = j;
                    found = true;
                    break;
                }
            } catch (...) {
                // ignore
            }
        }

        // 3) scan all opchain.json files inside model directories for matching `name` field
        if (!found) {
            for (auto &entry : std::filesystem::directory_iterator(base)) {
                if (!entry.is_directory())
                    continue;
                std::filesystem::path oj = entry.path() / "opchain.json";
                if (!std::filesystem::exists(oj))
                    continue;
                std::ifstream ifs(oj);
                try {
                    nlohmann::json j = nlohmann::json::parse(ifs);
                    if (j.contains("name") && j["name"].is_string() &&
                        j["name"].get<std::string>() == name) {
                        out = j;
                        found = true;
                        break;
                    }
                } catch (...) {
                    // ignore
                }
            }
        }

        if (found)
            break;
    }

    if (!found) {
        // Not found in model.json; still attempt to find opchain by name
        // Search opchain bases for matching opchain.json name field or directory.
        for (const auto &base : opchain_bases) {
            if (!std::filesystem::exists(base) || !std::filesystem::is_directory(base))
                continue;

            // try directory named as `name`
            std::filesystem::path p1 = base / name;
            std::filesystem::path opj = p1 / "opchain.json";
            if (std::filesystem::exists(opj)) {
                std::ifstream ifs(opj);
                try {
                    nlohmann::json j = nlohmann::json::parse(ifs);
                    out = j;
                    found = true;
                    break;
                } catch (...) {
                }
            }

            // scan all opchain.json for name field
            for (auto &entry : std::filesystem::directory_iterator(base)) {
                if (!entry.is_directory())
                    continue;
                std::filesystem::path mj = entry.path() / "opchain.json";
                if (!std::filesystem::exists(mj))
                    continue;
                std::ifstream ifs(mj);
                try {
                    nlohmann::json j = nlohmann::json::parse(ifs);
                    if (j.contains("name") && j["name"].is_string() &&
                        j["name"].get<std::string>() == name) {
                        out = j;
                        found = true;
                        break;
                    }
                } catch (...) {
                    // ignore
                }
            }

            if (found)
                break;
        }

        if (!found) {
            out["error"] = "model.json or opchain.json not found";
            res.set_content(out.dump(), "application/json");
            return;
        }
    }

    // If we found model.json earlier but prefer opchain description, try to
    // discover an opchain in model directory or opchains that matches and
    // merge its fields (so description placed in opchain.json is returned).
    {
        // try model dirs for opchain.json
        for (const auto &base : model_bases) {
            std::filesystem::path p = base / name / "opchain.json";
            if (std::filesystem::exists(p)) {
                std::ifstream ifs(p);
                try {
                    nlohmann::json oc = nlohmann::json::parse(ifs);
                    // prefer opchain 'description' if present
                    if (oc.contains("description")) {
                        out["description"] = oc["description"];
                    }
                    // expose opchain under key for frontend if needed
                    out["opchain"] = oc;
                } catch (...) {
                }
                break;
            }
        }

        // try opchain bases as well
        if (!out.contains("description")) {
            for (const auto &base : opchain_bases) {
                std::filesystem::path p = base / name / "opchain.json";
                if (std::filesystem::exists(p)) {
                    std::ifstream ifs(p);
                    try {
                        nlohmann::json oc = nlohmann::json::parse(ifs);
                        if (oc.contains("description")) {
                            out["description"] = oc["description"];
                        }
                        out["opchain"] = oc;
                    } catch (...) {
                    }
                    break;
                }
            }
        }
    }

    res.set_content(out.dump(), "application/json");
}
