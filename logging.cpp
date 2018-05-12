//
// Created by robert on 5/12/18.
//

#include <cstdarg>
#include "logging.h"

static bool enabled_error = true;
static bool enabled_notice = true;
static bool enabled_debug = false;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void log_error(const char* format, ...)
{
    if(enabled_error) {
        va_list argptr;
        va_start(argptr, format);

        pthread_mutex_lock(&mutex);
        vfprintf(stdout, format, argptr);
        fwrite("\n", 1, 1, stdout);
        fflush(stdout);
        pthread_mutex_unlock(&mutex);

        va_end(argptr);
    }
}

void log_notice(const char* format, ...)
{
    if(enabled_notice) {
        va_list argptr;
        va_start(argptr, format);
        pthread_mutex_lock(&mutex);
        vfprintf(stdout, format, argptr);
        fwrite("\n", 1, 1, stdout);
        fflush(stdout);
        pthread_mutex_unlock(&mutex);
        va_end(argptr);
    }
}

void log_debug(const char* format, ...)
{
    if(enabled_debug) {
        va_list argptr;
        va_start(argptr, format);

        pthread_mutex_lock(&mutex);
        vfprintf(stdout, format, argptr);
        fwrite("\n", 1, 1, stdout);
        fflush(stdout);
        pthread_mutex_unlock(&mutex);

        va_end(argptr);
    }
}
