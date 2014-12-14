#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <mqueue.h>
#include <assert.h>
#include <errno.h>
#include <sys/time.h>
#include <netinet/in.h>

#include "system.h"
#include "pkt.h"
#include "desc.h"
#include "pipe.h"
#include "thread_priority.h"
#include "audio_sink.h"

typedef enum
{
    STATE_INACTIVE,
    STATE_ACTIVE

} eSTATE;

// Decoder control block
typedef struct
{
    pthread_t   audio_scheduler_thread;
    eSTATE      state;

} saudio_SCHEDULER_CBLK;


// Decoder control block
static saudio_SCHEDULER_CBLK f_cblk;


static UINT32 audio_total_remaining_ms(
    void
    )
{

    UINT32  data_left = pipe_len_get(VRDMA_LPCM_SLICE) * 10;
    UINT32  queued_left = audio_sink_ms_left_get();

    return (data_left + queued_left);
}


static void inline audio_endianness_convert(
    UINT16 *temp,
    UINT32  samples
    )
{
    UINT32 i;


    for(i = 0; i < samples; i++)
    {
        temp[i] = ntohs(temp[i]);
    }
}


static void audio_scheduler_slice_dump(
    sdesc  *slice_head
    )
{
    UINT8      *curr_ptr;
    UINT32      bytes_left;
    UINT32      afc;
    UINT32      pid;
    UINT32      pes_byte_count;
    UINT32      payload_size;
    UINT32      start_offset;
    UINT32      copy_index;
    UINT32      samples_left;
    UINT32      ms_left;
    sdesc   *desc;


    static UINT8   playback_speed = 1;

    ms_left = audio_total_remaining_ms();
    if(ms_left > (100 + system_DELAY_MS))
    {
        if(playback_speed != 2)
        {
            audio_sink_playback_speed_inc();

            playback_speed = 2;
        }
    }
    else if(ms_left < (50 + system_DELAY_MS))
    {
        if(playback_speed != 0)
        {
            audio_sink_playback_speed_dec();

            playback_speed = 0;
        }
    }
    else
    {
        if(playback_speed != 1)
        {
            audio_sink_playback_speed_reset();

            playback_speed = 1;
        }
    }

    UINT8 *buf = audio_sink_buffer_get();

    copy_index = 0;

    desc = slice_head;
    do
    {
        // Get current
        curr_ptr = desc->data;

        // Get data left
        bytes_left = desc->data_len;
        assert(bytes_left > sizeof(sRTP_HDR));

        // Get TS header
        curr_ptr += sizeof(sRTP_HDR);

        // Get TS bytes left
        bytes_left -= sizeof(sRTP_HDR);
        assert((bytes_left % sizeof(sMPEG2_TS)) == 0);

        pes_byte_count = 0;
        do
        {
            sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;

            afc = AFC_GET(ts->hdr);
            pid = PID_GET(ts->hdr);

            if(pid == 0x1100)
            {
                UINT8 stuffing = 0; 
                if(afc & 0x02)
                {
                    stuffing = 1 + ts->payload.payload[0]; 
                }

                start_offset = stuffing; 

                if(PUSI_GET(ts->hdr))
                {
                     start_offset += 20;
                }

                payload_size = sizeof(sMPEG2_TS_PAYLOAD) - start_offset;


#if 0

#endif 

                memcpy(&buf[copy_index],
                        &ts->payload.payload[start_offset],
                        payload_size);

                copy_index += payload_size;
            }

            curr_ptr += sizeof(sMPEG2_TS);
            bytes_left -= sizeof(sMPEG2_TS);

        } while (bytes_left > 0);

        desc = desc->next;

    } while (desc != NULL);

    desc_put(slice_head);

    assert(copy_index == 1920);

    audio_endianness_convert((UINT16 *) buf, 960);

    // Push to decoder hardware
    audio_sink_buffer_set(buf, 1920);
}


void audio_scheduler_thread(
    void * arg
    )
{
    sdesc   *desc;


    while(1)
    {
        if(f_cblk.state == STATE_INACTIVE)
        {
            UINT32 len = pipe_len_get(VRDMA_LPCM_SLICE);
            if(len > (system_DELAY_MS / 10))
            {
                f_cblk.state = STATE_ACTIVE;

                printf("(audio_scheduler): Transition to active. [len = %u]\n", len);

                goto next_iter;
            }
        }
        else
        {
            UINT32  data_left_ms = pipe_len_get(VRDMA_LPCM_SLICE) * 10;
            UINT32  queued_ms = audio_sink_ms_left_get();

            if((data_left_ms + queued_ms) == 0)
            {
                f_cblk.state = STATE_INACTIVE;

                printf("(audio_scheduler): Transition to inactive.\n");

                goto next_iter;
            }

            if(queued_ms < 200)
            {
                UINT32  slices_to_queue = (200 - queued_ms + 10) / 10;
                do
                {
                    // Get slice
                    desc = pipe_get(VRDMA_LPCM_SLICE);
                    if(desc == NULL)
                    {
                        goto next_iter;
                    }

                    // Dump slice
                    audio_scheduler_slice_dump(desc);

                    slices_to_queue--;

                } while(slices_to_queue > 0);
            }
        }

        next_iter:

        usleep(5*1000);
    }
}


static void resources_init(
    void
    )
{
    // Initialize decoder hardware
    audio_sink_init();
}


static void audio_scheduler_thread_create(
    void
    )
{
    thread_create(&f_cblk.audio_scheduler_thread,
                     &audio_scheduler_thread,
                     NULL,
                     audio_SCHEDULER_THREAD_PRIORITY);
}


void audio_scheduler_init(
    void
    )
{
    // Initialize common resources
    resources_init();

    printf("(audio_scheduler): Init.\n");
}


void audio_scheduler_open(
    void
    )
{
    // Initialize decoder thread
    audio_scheduler_thread_create();

    printf("(audio_scheduler): Open.\n");
}


void audio_scheduler_close(
    void
    )
{
}
