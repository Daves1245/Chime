#include <ctype.h>
#include <stdio.h>

#include <utils/functions.h>

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

/*
 * int isinteger(str) - check if str is a number
 *
 * returns:
 *  true if str contains a valid, positive integer
 *  false otherwise
 */
int isinteger(const char *str) {
    for (const char *c = str; *c != '\0'; c++) {
        if (!isdigit(*c)) {
            return 0;
        }
    }
    return 1;
}

