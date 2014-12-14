#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "types.h"
#include "env2.h"

typedef struct
{
    eenv2_VAR   var;
    char           *str;

} senv2_VAR_ENTRY;


static senv2_VAR_ENTRY f_env_var_table[] =
{
        { env2_VAR_WIDTH, "displayWidth" },
        { env2_VAR_HEIGHT, "displayHeight" },
        { env2_VAR_SESSION_WIDTH, "sessionWidth" },
        { env2_VAR_SESSION_HEIGHT, "sessionHeight" }
};
#define env2_VAR_TABLE_SIZE (sizeof(f_env_var_table)/sizeof(f_env_var_table[0]))


static UINT32 f_env_vars[env2_VAR_MAX] = {0};


void env2_init(
    void
    )
{
    FILE * read_ptr = fopen("env.txt", "r");
    assert(read_ptr != NULL);

    char line[128];
    UINT32  i;
    UINT8   found;
    UINT32  value;

    while(fgets(line, sizeof(line), read_ptr) != NULL)
    {
        line[strlen(line) - 1] = 0;

        found = 0;

        for(i = 0; i < env2_VAR_TABLE_SIZE; i++)
        {
            if(strstr(line, f_env_var_table[i].str) != NULL)
            {
                printf("found!!\n");

                found = 1;

                break;
            }
        }

        if(found)
        {
            sscanf(line, "%*s %d", &value);

            f_env_vars[f_env_var_table[i].var] = value;

            printf("value = %u\n", value);
        }
    }
}


void env2_open(
    void
    )
{

}


UINT32 env2_get(
    eenv2_VAR   var
    )
{
    printf("(env2_get(): var = %u\n", f_env_vars[var]);

    return f_env_vars[var];
}
