#include "types.h"
#include "queue.h"
#include "pipe.h"

typedef struct
{
    queue    queue[VRDMA_MAX];

} sVRDMA_CBLK;


static sVRDMA_CBLK f_cblk;


void pipe_init(
    void
    )
{
    UINT32  i;


    for(i = 0; i < VRDMA_MAX; i++)
    {
        // Create queue
        f_cblk.queue[i] = queue_create();
    }
}


void pipe_put(
    UINT32  index,
    void   *data
    )
{
    queue_push(f_cblk.queue[index], data);
}


void * pipe_get(
    UINT32  index
    )
{
    return queue_pull(f_cblk.queue[index]);
}


unsigned int pipe_len_get(
    UINT32  index
    )
{
    return queue_len_get(f_cblk.queue[index]);
}
