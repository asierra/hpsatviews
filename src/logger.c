/* High Performance Satellite Views - Structured Logging System Implementation
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

// ANSI color codes for terminal output
static const char* LEVEL_COLORS[] = {
    "\033[37m",      // TRACE: Light gray
    "\033[36m",      // DEBUG: Cyan  
    "\033[32m",      // INFO:  Green
    "\033[33m",      // WARN:  Yellow
    "\033[31m",      // ERROR: Red
    "\033[35;1m"     // FATAL: Bold magenta
};

static const char* LEVEL_NAMES[] = {
    "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"
};

static const char* COLOR_RESET = "\033[0m";

// Global logger configuration
static LoggerConfig g_config = {
    .min_level = LOG_INFO,
    .use_colors = true,
    .log_to_file = false,
    .log_to_console = true,
    .include_timestamp = true,
    .include_location = true,
    .log_file = NULL,
    .log_filename = {0}
};

// Initialize logger with simple level setting
void logger_init(LogLevel min_level) {
    g_config.min_level = min_level;
    g_config.use_colors = isatty(STDERR_FILENO); // Auto-detect if terminal supports colors
}

// Initialize logger with full configuration
void logger_init_with_config(const LoggerConfig *config) {
    if (config == NULL) return;
    
    g_config = *config;
    
    // Open log file if specified
    if (g_config.log_to_file && strlen(g_config.log_filename) > 0) {
        g_config.log_file = fopen(g_config.log_filename, "a");
        if (g_config.log_file == NULL) {
            fprintf(stderr, "Warning: Failed to open log file '%s'\n", g_config.log_filename);
            g_config.log_to_file = false;
        }
    }
}

// Set minimum logging level
void logger_set_level(LogLevel level) {
    if (level >= LOG_TRACE && level < LOG_LEVEL_COUNT) {
        g_config.min_level = level;
    }
}

// Set log file (opens the file for appending)
void logger_set_file(const char *filename) {
    if (filename == NULL) return;
    
    // Close existing file if open
    if (g_config.log_file != NULL) {
        fclose(g_config.log_file);
        g_config.log_file = NULL;
    }
    
    // Copy filename and open new file
    strncpy(g_config.log_filename, filename, sizeof(g_config.log_filename) - 1);
    g_config.log_filename[sizeof(g_config.log_filename) - 1] = '\0';
    
    g_config.log_file = fopen(filename, "a");
    if (g_config.log_file != NULL) {
        g_config.log_to_file = true;
    } else {
        fprintf(stderr, "Warning: Failed to open log file '%s'\n", filename);
        g_config.log_to_file = false;
    }
}

// Enable or disable colors
void logger_enable_colors(bool enable) {
    g_config.use_colors = enable && isatty(STDERR_FILENO);
}

// Clean up logger resources
void logger_cleanup(void) {
    if (g_config.log_file != NULL) {
        fclose(g_config.log_file);
        g_config.log_file = NULL;
    }
    g_config.log_to_file = false;
}

// Get current timestamp string
static void get_timestamp(char *buffer, size_t buffer_size) {
    struct timeval tv;
    struct tm *tm_info;
    
    gettimeofday(&tv, NULL);
    tm_info = localtime(&tv.tv_sec);
    
    snprintf(buffer, buffer_size, "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             tv.tv_usec / 1000);
}

// Extract filename from full path
static const char* extract_filename(const char *filepath) {
    const char *filename = strrchr(filepath, '/');
    return filename ? filename + 1 : filepath;
}

// Main logging function
void logger_log(LogLevel level, const char *file, int line, const char *format, ...) {
    // Check if this level should be logged
    if (level < g_config.min_level) {
        return;
    }
    
    // Validate level
    if (level < 0 || level >= LOG_LEVEL_COUNT) {
        return;
    }
    
    va_list args;
    char timestamp[64] = {0};
    char message[1024];
    char final_message[1200];
    
    // Format the user message
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    // Build the final log message
    char *pos = final_message;
    size_t remaining = sizeof(final_message);
    int written;
    
    // Add timestamp if enabled
    if (g_config.include_timestamp) {
        get_timestamp(timestamp, sizeof(timestamp));
        written = snprintf(pos, remaining, "[%s] ", timestamp);
        pos += written;
        remaining -= written;
    }
    
    // Add level name
    written = snprintf(pos, remaining, "%s: ", LEVEL_NAMES[level]);
    pos += written;
    remaining -= written;
    
    // Add location if enabled
    if (g_config.include_location && file != NULL) {
        written = snprintf(pos, remaining, "(%s:%d) ", extract_filename(file), line);
        pos += written;
        remaining -= written;
    }
    
    // Add the actual message
    written = snprintf(pos, remaining, "%s", message);
    pos += written;
    remaining -= written;
    
    // Output to console if enabled
    if (g_config.log_to_console) {
        if (g_config.use_colors) {
            fprintf(stderr, "%s%s%s\n", LEVEL_COLORS[level], final_message, COLOR_RESET);
        } else {
            fprintf(stderr, "%s\n", final_message);
        }
        fflush(stderr);
    }
    
    // Output to file if enabled
    if (g_config.log_to_file && g_config.log_file != NULL) {
        fprintf(g_config.log_file, "%s\n", final_message);
        fflush(g_config.log_file);
    }
    
    // For FATAL level, terminate the program
    if (level == LOG_FATAL) {
        logger_cleanup();
        exit(EXIT_FAILURE);
    }
}