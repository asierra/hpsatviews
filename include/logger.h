/* Structured logging system with level filtering and file output.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_LOGGER_H_
#define HPSATVIEWS_LOGGER_H_

#include <stdio.h>
#include <stdbool.h>

// Log severity levels (ascending order).
typedef enum {
    LOG_TRACE = 0,    // very verbose, for detailed diagnosis
    LOG_DEBUG = 1,    // debugging information
    LOG_INFO = 2,     // general execution progress
    LOG_WARN = 3,     // recoverable issues
    LOG_ERROR = 4,    // serious errors
    LOG_FATAL = 5,    // unrecoverable errors
    LOG_LEVEL_COUNT = 6
} LogLevel;

// Logger configuration.
typedef struct {
    LogLevel min_level;
    bool use_colors;            // ANSI color output in terminal
    bool log_to_file;
    bool log_to_console;
    bool include_timestamp;
    bool include_location;      // prepend file:line to each message
    FILE *log_file;
    char log_filename[256];
} LoggerConfig;

void logger_init(LogLevel min_level);
void logger_init_with_config(const LoggerConfig *config);
void logger_set_level(LogLevel level);
void logger_set_file(const char *filename);
void logger_enable_colors(bool enable);
void logger_cleanup(void);

// Low-level log function; prefer the macros below.
void logger_log(LogLevel level, const char *file, int line, 
                const char *format, ...);

// Logging macros (file:line auto-injected).
#define LOG_TRACE(...) logger_log(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) logger_log(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  logger_log(LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  logger_log(LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) logger_log(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...) logger_log(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

// NetCDF error-check macros.
#define NC_CHECK(call) do { \
    int _nc_ret = (call); \
    if (_nc_ret != 0) { \
        LOG_ERROR("NetCDF error in %s: %s", #call, nc_strerror(_nc_ret)); \
        return _nc_ret; \
    } \
} while(0)

#define NC_WARN(call) do { \
    int _nc_ret = (call); \
    if (_nc_ret != 0) { \
        LOG_WARN("NetCDF warning in %s: %s", #call, nc_strerror(_nc_ret)); \
    } \
} while(0)

// Allocation failure guard: logs FATAL and exits if ptr is NULL.
#define MALLOC_CHECK(ptr, size) do { \
    if ((ptr) == NULL) { \
        LOG_FATAL("Memory allocation failed: %zu bytes", (size_t)(size)); \
        exit(EXIT_FAILURE); \
    } else { \
        LOG_TRACE("Allocated %zu bytes at %p", (size_t)(size), (ptr)); \
    } \
} while(0)

// Timing macro; emits a [PERF]-tagged DEBUG entry.
#define LOG_TIMING(elapsed_s, fmt, ...) \
    LOG_DEBUG("[PERF] " fmt ": %.3f s", ##__VA_ARGS__, (double)(elapsed_s))

#define LOG_IF(condition, level, ...) do { \
    if (condition) { \
        logger_log(level, __FILE__, __LINE__, __VA_ARGS__); \
    } \
} while(0)

#endif /* HPSATVIEWS_LOGGER_H_ */