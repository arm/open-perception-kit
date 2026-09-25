/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "websocket_writer.h"

#include <utility>

#include <gst/gst.h>

bool WebSocketWriter::validate(const OpkCommConnectionHdl &hdl) {
    if (!m_ws) {
        return false;
    }

    auto con = m_ws->get_con_from_hdl(hdl);
    const auto resource = con->get_resource();
    const bool ok = resource == m_endpoint;
    if (!ok) {
        GST_INFO_OBJECT(self(),
                        "Rejected opkcomm WebSocket connection for endpoint '%s' (expected '%s')",
                        resource.c_str(),
                        m_endpoint.c_str());
    }
    return ok;
}

void WebSocketWriter::on_open(const OpkCommConnectionHdl &hdl) {
    // A handshake accepted before stop_listening() can complete during shutdown.
    if (!m_ws->is_listening()) {
        websocketpp::lib::error_code ec;
        m_ws->close(hdl, websocketpp::close::status::going_away, "", ec);
        return;
    }
    std::lock_guard<std::mutex> lock(m_connection_lock);
    m_connections.insert(hdl);
    GST_INFO_OBJECT(self(), "opkcomm WebSocket client connected");
}

void WebSocketWriter::on_close(const OpkCommConnectionHdl &hdl) {
    std::lock_guard<std::mutex> lock(m_connection_lock);
    m_connections.erase(hdl);
    GST_INFO_OBJECT(self(), "opkcomm WebSocket client disconnected");
}

bool WebSocketWriter::io_open() {
    if (m_endpoint.empty() || m_endpoint.front() != '/') {
        GST_WARNING_OBJECT(self(), "endpoint must start with '/': '%s'", m_endpoint.c_str());
        return false;
    }

    try {
        m_ws = std::make_shared<OpkCommWsServer>();
        m_ws->clear_access_channels(websocketpp::log::alevel::all);
        m_ws->clear_error_channels(websocketpp::log::elevel::all);
        m_ws->init_asio();
        m_ws->set_reuse_addr(true);
        m_ws->set_validate_handler(
            [this](const OpkCommConnectionHdl &hdl) { return validate(hdl); });
        m_ws->set_open_handler([this](const OpkCommConnectionHdl &hdl) { on_open(hdl); });
        m_ws->set_close_handler([this](const OpkCommConnectionHdl &hdl) { on_close(hdl); });
        m_ws->listen(m_port);
        m_ws->start_accept();
        m_ws_thread = std::thread([server = m_ws] { server->run(); });

        GST_INFO_OBJECT(self(),
                        "opkcomm WebSocket server listening on port %u endpoint '%s'",
                        static_cast<unsigned>(m_port),
                        m_endpoint.c_str());
        return true;
    } catch (const std::exception &e) {
        GST_WARNING_OBJECT(self(), "Failed to start opkcomm WebSocket server: %s", e.what());
        io_close();
        return false;
    }
}

void WebSocketWriter::io_close() {
    if (m_ws) {
        m_ws->get_io_service().post([this] {
            websocketpp::lib::error_code ec;
            m_ws->stop_listening(ec);
            std::lock_guard<std::mutex> lock(m_connection_lock);
            for (const auto &hdl : m_connections) {
                m_ws->close(hdl, websocketpp::close::status::going_away, "", ec);
            }
        });
    }

    if (m_ws_thread.joinable()) {
        m_ws_thread.join();
    } else if (m_ws) {
        m_ws->run(); // Startup may have failed before the worker was created.
    }

    std::lock_guard<std::mutex> lock(m_connection_lock);
    m_connections.clear();
    m_ws.reset();
}

bool WebSocketWriter::publish(const std::string &json_str) {
    if (!m_ws) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_connection_lock);
    for (const auto &hdl : m_connections) {
        websocketpp::lib::error_code ec;
        m_ws->send(hdl, json_str, websocketpp::frame::opcode::text, ec);
        if (ec) {
            GST_INFO_OBJECT(self(), "opkcomm WebSocket send failed: %s", ec.message().c_str());
        }
    }

    return true;
}
