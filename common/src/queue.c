#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <assert.h>
#include <stdio.h>

#include "types.h"
#include "queue.h"

typedef struct sNODE
{
    struct sNODE   *next;
    void           *data;

} sNODE;


typedef struct
{
    sNODE          *head;
    sNODE          *tail;
    pthread_mutex_t lock;
    sem_t           sem;
    unsigned int    len;
    UINT32          high_water_mark;

} sQUEUE;


queue queue_create(
    void
    )
{
    sQUEUE *queue = malloc(sizeof(sQUEUE));

    pthread_mutex_init(&queue->lock, NULL);

    sem_init(&queue->sem, 0, 0);

    queue->head = NULL;
    queue->tail = NULL;
    queue->len  = 0;

    return queue;
}


void queue_destroy(
    queue    queue_id
    )
{
    sQUEUE *queue;
    sNODE  *curr;
    sNODE  *next;


    // Get the queue
    queue = queue_id;

    // Free every node
    curr = queue->head;
    while(curr)
    {
        next = curr->next;

        // Free data
        free(curr->data);

        // Free node
        free(curr);

        curr = next;
    }

    // Destroy mutex
    pthread_mutex_destroy(&queue->lock);

    // Free queue
    free(queue);
}


void queue_push(
    queue    queue_id,
    void       *data
    )
{
    sNODE  *node;
    sQUEUE *queue;


    assert(data != NULL);

    // Get queue
    queue = queue_id;

    // Construct new node
    node = malloc(sizeof(sNODE));
    node->data = data;
    node->next = NULL;

    // Lock
    pthread_mutex_lock(&queue->lock);

#if 0

#endif

    // Is list empty?
    if(queue->head == NULL)
    {
        // List is empty, insert only node
        queue->head = node;

        queue->tail = node;

        goto cleanup;
    }

    // Append to end
    queue->tail->next = node;

    // Update tail
    queue->tail = node;

cleanup:

    queue->len++;

    pthread_mutex_unlock(&queue->lock);
}


void * queue_pull(
    queue    queue_id
    )
{
    sQUEUE         *queue;
    unsigned char  *data;


    // Get queue
    queue = queue_id;

    // Lock
    pthread_mutex_lock(&queue->lock);

#if 0

#endif

    // Is list empty?
    if(queue->head == NULL)
    {
        assert(queue->tail == NULL);

        // List is empty
        data = NULL;

        goto cleanup;
    }

    if(queue->head == queue->tail)
    {
        assert(queue->head->next == NULL);
        assert(queue->tail->next == NULL);

        // List only has one element
        data = queue->head->data;

        // Free the node
        free(queue->head);

        // Reset head and tail
        queue->head = NULL;
        queue->tail = NULL;

        goto cleanup;
    }

    // More than one element

    // Return packet
    data = queue->head->data;

    // Get next
    sNODE *next = queue->head->next;

    // Free head
    free(queue->head);

    // Advance head
    queue->head = next;

cleanup:

    if(data != NULL)
    {
        queue->len--;
    }

    pthread_mutex_unlock(&queue->lock);

    return data;
}


unsigned int queue_len_get(
    queue    queue_id
    )
{
    sQUEUE         *queue;
    unsigned int    len;


    // Get queue
    queue = queue_id;

    pthread_mutex_lock(&queue->lock);

    len = queue->len;

    pthread_mutex_unlock(&queue->lock);

    return len;
}
