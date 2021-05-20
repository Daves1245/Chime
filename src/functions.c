#include <ctype.h>
#include <stdio.h>

#include "functions.h"

int min(int a, int b) {
    return a < b ? a : b;
}

int max(int a, int b) {
    return a > b ? a : b;
}

int isnumber(const char *str) {
    while (*str != EOF && *str != '\0' && *str != '\n')  {
        if (!isdigit(*str)) {
            return 0;
        }
    }
    return 1;
}

