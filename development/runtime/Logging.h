/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <vector>

namespace pek::runtime {

/**
 * Runtime-facing PEK log verbosity.
 *
 * The values match the process-wide PEK logger and OPK_LOG_LEVEL:
 * Off disables normal log records, Error includes only errors, and each higher
 * value includes the less verbose levels below it.
 *
 * Logging configuration is process-wide. It is not owned by, or scoped to, one
 * Pipeline or OpChain instance.
 */
enum class LogLevel : int { Off = 0, Error = 1, Warn = 2, Notice = 3, Info = 4, Debug = 5 };

/**
 * Runtime-facing PEK log output target.
 *
 * Enabled log records are written to every active target. Severity does not
 * imply a target; for example, errors only go to stderr when Stderr is enabled.
 */
enum class LogTarget { Stdout, Stderr, File };

/**
 * Return the current process-wide PEK log level.
 */
LogLevel getLogLevel();

/**
 * Set the current process-wide PEK log level using the runtime enum.
 */
void setLogLevel(LogLevel logLevel);

/**
 * Set the current process-wide PEK log level using the numeric logger scale.
 *
 * Values outside the supported range are clamped by the underlying PEK logger.
 */
void setLogLevel(int logLevel);

/**
 * Return the currently enabled process-wide PEK log targets.
 */
std::vector<LogTarget> getEnabledLogTargets();

/**
 * Enable or disable one process-wide PEK log target.
 *
 * Returns false only when the underlying logger cannot address the requested
 * target.
 */
bool setLogTargetState(LogTarget target, bool enabled);

/**
 * Flush accepted PEK log records to the currently enabled targets.
 */
void flushLog();

} // namespace pek::runtime
