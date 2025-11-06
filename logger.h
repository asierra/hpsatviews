/* High Performance Satellite Views - Structured Logging System
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_LOGGER_H_
#define HPSATVIEWS_LOGGER_H_

#include <stdio.h>
#include <stdbool.h>

// Log levels in order of severity (lower number = more verbose)
typedef enum {
    LOG_TRACE = 0,    // Very detailed information, typically for diagnosis
    LOG_DEBUG = 1,    // Detailed information for debugging
    LOG_INFO = 2,     // General information about program execution
    LOG_WARN = 3,     // Warning messages for recoverable issues
    LOG_ERROR = 4,    // Error messages for serious problems
    LOG_FATAL = 5,    // Fatal errors that cause program termination
    LOG_LEVEL_COUNT = 6
} LogLevel;

// Logger configuration structure
typedef struct {
    LogLevel min_level;          // Minimum level to log
    bool use_colors;            // Enable ANSI colors in terminal
    bool log_to_file;           // Enable file logging
    bool log_to_console;        // Enable console logging
    bool include_timestamp;     // Include timestamp in output
    bool include_location;      // Include file:line information
    FILE *log_file;            // File handle for logging (NULL = no file)
    char log_filename[256];    // Path to log file
} LoggerConfig;

// Core logging functions
void logger_init(LogLevel min_level);
void logger_init_with_config(const LoggerConfig *config);
void logger_set_level(LogLevel level);
void logger_set_file(const char *filename);
void logger_enable_colors(bool enable);
void logger_cleanup(void);

// Main logging function (usually called via macros)
void logger_log(LogLevel level, const char *file, int line, 
                const char *format, ...);

// Convenience macros for easy logging with automatic file:line info
#define LOG_TRACE(...) logger_log(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) logger_log(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  logger_log(LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  logger_log(LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) logger_log(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...) logger_log(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

// Specialized macros for NetCDF operations (replacing ERR/WRN)
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

// Memory allocation logging helpers
#define MALLOC_CHECK(ptr, size) do { \
    if ((ptr) == NULL) { \
        LOG_FATAL("Memory allocation failed: %zu bytes", (size_t)(size)); \
        exit(EXIT_FAILURE); \
    } else { \
        LOG_TRACE("Allocated %zu bytes at %p", (size_t)(size), (ptr)); \
    } \
} while(0)

// Performance timing helpers
#define LOG_TIMING(name, start_time, end_time) \
    LOG_DEBUG("Performance: %s took %.6f seconds", (name), (end_time) - (start_time))

// Conditional logging (only if condition is true)
#define LOG_IF(condition, level, ...) do { \
    if (condition) { \
        logger_log(level, __FILE__, __LINE__, __VA_ARGS__); \
    } \
} while(0)

#endif /* HPSATVIEWS_LOGGER_H_ */