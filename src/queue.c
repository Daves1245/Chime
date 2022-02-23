#include "queue.h"

struct event *q_front(struct list_head *q) {
    return (struct event *) list_entry(q->next, struct event, list);
}

void q_push(struct list_head *q, struct event *e) {
    list_add(&e->list, q->next);
}

void q_pop(struct list_head *q) {
    list_del(q->next);
}
