#if !defined(_ENV2_H_)
#define _ENV2_H_

#include "types.h"

typedef enum
{
    env2_VAR_WIDTH,
    env2_VAR_HEIGHT,
    env2_VAR_SESSION_WIDTH,
    env2_VAR_SESSION_HEIGHT,

    env2_VAR_MAX

} eenv2_VAR;

extern void env2_init(
    void
    );

extern void env2_open(
    void
    );

extern UINT32 env2_get(
    eenv2_VAR   var
    );

#endif
