#include <stdlib.h>
#include <assert.h>

#include "desc.h"

sdesc * desc_get(
    void
    )
{
    sdesc *desc = malloc(sizeof(sdesc));
    assert(desc != NULL);

    desc->data      = NULL;
    desc->data_len  = 0;
    desc->next      = NULL;

    return desc;
}


void desc_put(
     sdesc  *desc
     )
{
     sdesc  *curr;
     sdesc  *next;


     // Consistency check
     assert(desc != NULL);

     // Initialize current
     curr = desc;

     // Release descriptors and data
     while(curr != NULL)
     {
         next = curr->next;

         free(curr->data);
         free(curr);

         curr = next;
     }
}
