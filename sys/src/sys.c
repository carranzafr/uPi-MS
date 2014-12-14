#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <mqueue.h>
#include <fcntl.h>

#include "thread_priority.h"
#include "sys.h"
#include "m2ts_decoder.h"
#include "video_decoder.h"
#include "data.h"
#include "ctl.h"


static pthread_t f_sys_thread;

static void resource_init(
    void
    )
{
#if 0

#endif
}


static void wait_forever(
    void
    )
{
    pthread_join(f_sys_thread, NULL);
}


void sys_init(
    void
    )
{
    // Initialize system manager
    resource_init(); 

    // Initialize VRDM
    pipe_init();

    // Initialize Data manager
    data_init();

    // Initialize m2ts decoder.
    m2ts_decoder_init();

    // Initialize video decoder module
    video_decoder_init();

    video_scheduler_init();

    // Initialize audio decoder module
    audio_decoder_init();

    audio_scheduler_init();

    printf("(sys): (sys_init): Done.\n");
}


static void sys_thread(
    void * arg
    )
{
    do
    {
        usleep(1*1000*1000);

    } while(1);
}


void sys_open(
    void
    )
{
    // Initialize Data manager
    data_open();

    // Initialize m2ts decoder
    m2ts_decoder_open();

    // Initialize video decoder module
    video_decoder_open();

    // Initialize video scheduler
    video_scheduler_open();

    // Initialize video decoder module
    audio_decoder_open();

    // Initialize audio scheduler
    audio_scheduler_open();

    printf("(sys): (sys_open): Done.\n");

    thread_create(&f_sys_thread, &sys_thread, NULL, sys_THREAD_PRIORITY);

    // Wait forever
    wait_forever();
}
