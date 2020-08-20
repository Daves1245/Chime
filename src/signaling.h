#ifndef SIGNALING_H
#define SIGNALING_H

#include <signal.h>

/*
 * If the connected flag is unset via a
 * caught signal, we disconnect gracefully */
volatile sig_atomic_t connected = 1;

#endif
