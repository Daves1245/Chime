#include <time.h>

#include "logging.h"
#include "colors.h"

FILE *log_output_stream = NULL;

/*
 * logs(string) - log a message
 * params: str; message to be logged
 *
 * Log a message to the log output stream
 * (stdout by default)
 *
 * Return: 
 * * OK on success
 * * ERROR_NOT_INITIALIZED if log_output_stream is not initialized
 */
STATUS logs(const char *str) {
    time_t rtime;
    struct tm *now;

    if (!log_output_stream) {
        return ERROR_NOT_INITIALIZED;
    }

    time(&rtime);
    now = localtime(&rtime);
    fprintf(log_output_stream, CYAN "[%d:%02d]: " ANSI_RESET "%s\n" ANSI_RESET, now->tm_hour, now->tm_min, str);
    return OK;
}

/*
 * log_init(FILE *output_stream) - Initialize logging resources
 *
 * Some resources used while logging (such as the
 * log output stream) cannot be initialized statically and
 * thus must be initialized at runtime. This function
 * must be called before any usage of the log functions.
 */
void log_init(FILE *output_stream) {
    log_output_stream = output_stream;
}
