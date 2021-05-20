#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include "status.h"
#include "types.h"

/* A log entry's max length */
#define LOGBUFF_STR_LEN 500

/* Log entries will be sent through here */
FILE *log_output_stream;

STATUS logs(const char *str); /* Log a message to the output stream */
void log_init(FILE *output_stream);          /* Initialize logging resources */

#endif
