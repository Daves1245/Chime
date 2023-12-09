#ifndef FT_REQUEST_H
#define FT_REQUEST_H

#include <limits.h>
#include <sys/stat.h>

typedef enum FT_STATUS {
    UPLOAD, DOWNLOAD
} ft_status;

struct ft_request {
    ft_status status;
    char filename[NAME_MAX];
    struct stat stat_sb;
    struct ft_request *next, *prev;
};

struct ft_request *make_request(ft_status status, char *filename);
void push_request(struct ft_request *);
struct ft_request *peek_request(void);
struct ft_request *pop_request(void);

#endif
