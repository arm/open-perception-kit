/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

#pragma once

#include <vector>

namespace opk::runtime {

/**
 * Runtime-facing OPK log verbosity.
 *
 * The values match the process-wide OPK logger and OPK_LOG_LEVEL:
 * Off disables normal log records, Error includes only errors, and each higher
 * value includes the less verbose levels below it.
 *
 * Logging configuration is process-wide. It is not owned by, or scoped to, one
 * Pipeline or OpChain instance.
 */
enum class LogLevel : int { Off = 0, Error = 1, Warn = 2, Notice = 3, Info = 4, Debug = 5 };

/**
 * Runtime-facing OPK log output target.
 *
 * Enabled log records are written to every active target. Severity does not
 * imply a target; for example, errors only go to stderr when Stderr is enabled.
 */
enum class LogTarget { Stdout, Stderr, File };

/**
 * Return the current process-wide OPK log level.
 */
LogLevel getLogLevel();

/**
 * Set the current process-wide OPK log level using the runtime enum.
 */
void setLogLevel(LogLevel logLevel);

/**
 * Set the current process-wide OPK log level using the numeric logger scale.
 *
 * Values outside the supported range are clamped by the underlying OPK logger.
 */
void setLogLevel(int logLevel);

/**
 * Return the currently enabled process-wide OPK log targets.
 */
std::vector<LogTarget> getEnabledLogTargets();

/**
 * Enable or disable one process-wide OPK log target.
 *
 * Returns false only when the underlying logger cannot address the requested
 * target.
 */
bool setLogTargetState(LogTarget target, bool enabled);

/**
 * Flush accepted OPK log records to the currently enabled targets.
 */
void flushLog();

} // namespace opk::runtime
