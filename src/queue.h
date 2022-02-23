#include "list.h"

struct event {
    struct list_head list;
    int type;
};

struct event *q_front(struct list_head *q);
void q_push(struct list_head *q, struct event *e);
void q_pop(struct list_head *q);

