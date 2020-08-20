#ifndef STATUS_H
#define STATUS_H

/*
 * enum chime_status - Describe the return
 * status of a function.
 *
 * A collection of constants used to describe
 * the return status of any function that can
 * exit successfully or error. This is used
 * throughout the entire project.
 */
enum chime_status {
  OK,
  ERROR_USER_NOT_FOUND,
  ERROR_CONNECTION_REFUSED,
  ERROR_CONNECTION_DROPPED,
  ERROR_CONNECTION_LOST,
  ERROR_USERNAME_IN_USE,
  ERROR_INVALID_FILEPATH,
  ERROR_NOT_SENT,
  ERROR_FAILED_SYSCALL
};

#endif

