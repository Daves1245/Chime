#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include "status.h"
#include "types.h"

FILE *log_output_stream;

status logs(const char *str); /* Log a message to the output stream */
void log_init(void); /* Initialize logging resources */

#endif
