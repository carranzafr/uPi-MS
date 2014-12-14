#if !defined(_QUEUE_H_)
#define _QUEUE_H_

#define QUEUE    void *

extern queue queue_create(
    void
    );

extern void queue_destroy(
    queue    queue
    );

extern void queue_push(
    queue    queue,
    void       *data
    );

extern void * queue_pull(
    queue    queue
    );

extern unsigned int queue_len_get(
    queue    queue_id
    );

#endif // _QUEUE_H_
