#if !defined(_DESC_H_)
#define _DESC_H_

#include "types.h"

typedef struct sdesc
{
    UINT8              *data;
    UINT32              data_len;
    struct sdesc    *next;

} sdesc;

extern sdesc * desc_get(
    void
    );

extern void desc_put(
     sdesc  *desc
     );

#endif // _DESC_H_
