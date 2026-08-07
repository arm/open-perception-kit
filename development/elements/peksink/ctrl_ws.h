/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#ifndef __CTRL_WS_H__
#define __CTRL_WS_H__

#include <memory>
#include <mutex>
#include <set>
#include <string>

#define ASIO_STANDALONE
#include <asio.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtemplate-id-cdtor"

#include <websocketpp/common/connection_hdl.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/frame.hpp>
#include <websocketpp/server.hpp>

#include "status_reporter.h"

#pragma GCC diagnostic pop

struct _GstPekSink;

enum class CtrlSockerError {
    OK,
};

using ws_server = websocketpp::server<websocketpp::config::asio>;
using connection_hdl = websocketpp::connection_hdl;

class CtrlWebSocket {

    _GstPekSink *self_;

    std::thread ws_server_thread;

    std::shared_ptr<ws_server> ws = nullptr;

    std::unordered_map<std::string, std::shared_ptr<StatusReporter>> status_reporters;

    std::unordered_map<std::string, std::function<void(const nlohmann::json &)>> message_types;

    std::set<connection_hdl, std::owner_less<connection_hdl>> hdls;

    mutable std::mutex reporter_lock;
    mutable std::mutex hdl_lock;

    void play_pause(const nlohmann::json &jsn);
    void model_toggle(const nlohmann::json &jsn);

    void send_to_all(const std::string &text);

    void on_open(const connection_hdl &hdl);
    void on_close(const connection_hdl &hdl);
    void on_message(const connection_hdl &hdl, const ws_server::message_ptr &msg);

    CtrlSockerError setup();

  public:
    CtrlWebSocket() = default;
    CtrlWebSocket(_GstPekSink *self);

    CtrlSockerError start();
    CtrlSockerError stop();

    void register_status_reporter(const std::string &name, std::shared_ptr<StatusReporter> status);

    void report();
};

#endif // !__CTRL_WS_H__
