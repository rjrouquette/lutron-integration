//
// Created by robert on 5/12/18.
//

#ifndef LUTRON_INTEGRATION_LOGGING_H
#define LUTRON_INTEGRATION_LOGGING_H

#include <string>

void log_debug (const char *format, ...) __attribute__ ((__format__ (__printf__, 1, 2)));
void log_notice(const char *format, ...) __attribute__ ((__format__ (__printf__, 1, 2)));
void log_error (const char *format, ...) __attribute__ ((__format__ (__printf__, 1, 2)));

inline void log_debug (const std::string& text) { log_debug ("%s", text.c_str()); }
inline void log_notice(const std::string& text) { log_notice("%s", text.c_str()); }
inline void log_error (const std::string& text) { log_error ("%s", text.c_str()); }

#endif //LUTRON_INTEGRATION_LOGGING_H
