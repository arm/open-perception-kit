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

#include "websocket_writer.h"

#include "Log.h"

#include <utility>

#include <gst/gst.h>

bool WebSocketWriter::validate(const OpkCommConnectionHdl &connectionHandle) {
    if (!m_ws) {
        return false;
    }

    auto connection = m_ws->get_con_from_hdl(connectionHandle);
    const auto resource = connection->get_resource();
    const bool accepted = resource == m_endpoint;
    if (!accepted) {
        GST_INFO_OBJECT(self(),
                        "Rejected opkcomm WebSocket connection for endpoint '%s' (expected '%s')",
                        opk::log::escape(resource).c_str(),
                        opk::log::escape(m_endpoint).c_str());
    }
    return accepted;
}

void WebSocketWriter::on_open(const OpkCommConnectionHdl &connectionHandle) {
    // A handshake accepted before stop_listening() can complete during shutdown.
    if (!m_ws->is_listening()) {
        websocketpp::lib::error_code error;
        m_ws->close(connectionHandle, websocketpp::close::status::going_away, "", error);
        return;
    }
    std::scoped_lock lock(m_connection_lock);
    m_connections.insert(connectionHandle);
    GST_INFO_OBJECT(self(), "opkcomm WebSocket client connected");
}

void WebSocketWriter::on_close(const OpkCommConnectionHdl &connectionHandle) {
    std::scoped_lock lock(m_connection_lock);
    m_connections.erase(connectionHandle);
    GST_INFO_OBJECT(self(), "opkcomm WebSocket client disconnected");
}

bool WebSocketWriter::io_open() {
    if (m_endpoint.empty() || m_endpoint.front() != '/') {
        GST_WARNING_OBJECT(
            self(), "endpoint must start with '/': '%s'", opk::log::escape(m_endpoint).c_str());
        return false;
    }

    try {
        m_ws = std::make_shared<OpkCommWsServer>();
        m_ws->clear_access_channels(websocketpp::log::alevel::all);
        m_ws->clear_error_channels(websocketpp::log::elevel::all);
        m_ws->init_asio();
        m_ws->set_reuse_addr(true);
        m_ws->set_validate_handler([this](const OpkCommConnectionHdl &connectionHandle) {
            return validate(connectionHandle);
        });
        m_ws->set_open_handler(
            [this](const OpkCommConnectionHdl &connectionHandle) { on_open(connectionHandle); });
        m_ws->set_close_handler(
            [this](const OpkCommConnectionHdl &connectionHandle) { on_close(connectionHandle); });
        m_ws->listen(m_port);
        m_ws->start_accept();
        m_ws_thread = std::thread([server = m_ws] { server->run(); });

        GST_INFO_OBJECT(self(),
                        "opkcomm WebSocket server listening on port %u endpoint '%s'",
                        static_cast<unsigned>(m_port),
                        opk::log::escape(m_endpoint).c_str());
        return true;
    } catch (const std::exception &exception) {
        GST_WARNING_OBJECT(self(),
                           "Failed to start opkcomm WebSocket server: %s",
                           opk::log::escape(exception.what()).c_str());
        io_close();
        return false;
    }
}

void WebSocketWriter::io_close() {
    if (m_ws) {
        m_ws->get_io_service().post([this] {
            websocketpp::lib::error_code error;
            m_ws->stop_listening(error);
            std::scoped_lock lock(m_connection_lock);
            for (const auto &connectionHandle : m_connections) {
                m_ws->close(connectionHandle, websocketpp::close::status::going_away, "", error);
            }
        });
    }

    if (m_ws_thread.joinable()) {
        m_ws_thread.join();
    } else if (m_ws) {
        m_ws->run(); // Startup may have failed before the worker was created.
    }

    std::scoped_lock lock(m_connection_lock);
    m_connections.clear();
    m_ws.reset();
}

bool WebSocketWriter::publish(const std::string &record) {
    if (!m_ws) {
        return false;
    }

    std::scoped_lock lock(m_connection_lock);
    for (const auto &connectionHandle : m_connections) {
        websocketpp::lib::error_code error;
        m_ws->send(connectionHandle, record, websocketpp::frame::opcode::text, error);
        if (error) {
            GST_INFO_OBJECT(self(),
                            "opkcomm WebSocket send failed: %s",
                            opk::log::escape(error.message()).c_str());
        }
    }

    return true;
}
