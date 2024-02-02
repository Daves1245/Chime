#include <ft_request.h>

struct ft_request *make_request(ft_status status, char *filename) {
    struct ft_request *ret = malloc(sizeof(*ret));
    ret->status = status;
    strcpy(ret->filename, filename);
    if (stat(filename, &ret->stat_sb) == -1) {
        logs(CHIME_WARN "[make_request]: Could not stat file");
        perror("stat");
        exit(EXIT_FAILURE);
    }
    // TODO pushes onto the queue but returns too?
    push_request(ret);
    return ret;
}

void push_request(struct ft_request *req) {
    ft_request_queue->prev->next = req;
    req->prev = ft_request_queue->prev;
    req->next = ft_request_queue;
    ft_request_queue->prev = req;
}

struct ft_request *peek_request(void) {
    return ft_request_queue;
}

struct ft_request *pop_request(void) {
    //printf("Popped a request!\n");
    struct ft_request *ret = ft_request_queue;
    ft_request_queue = ft_request_queue->next;
    ret->prev->next = ft_request_queue;
    ft_request_queue->prev = ret->prev;
    if (ret == ret->next) {
        ft_request_queue = NULL;
    }
    return ret;
}

// TODO return 1?
int free_request(struct ft_request *req) {
    free(req);
    return 1;
}

