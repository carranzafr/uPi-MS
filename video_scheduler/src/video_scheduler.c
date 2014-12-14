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
#include "video_sink.h"

// Decoder control block
typedef struct
{
    pthread_t       video_scheduler_thread;
    pthread_t       slice_packing_thread;
    pthread_t       pcr_update_thread;
    UINT64          curr_time;
    pthread_mutex_t lock;

} svideo_SCHEDULER_CBLK;

// Decoder control block
static svideo_SCHEDULER_CBLK f_cblk;

static void video_scheduler_slice_dump(
    sdesc   *slice_head
    )
{
    UINT8      *curr_ptr;
    UINT32      bytes_left;
    UINT32      afc;
    UINT32      pes_byte_count;
    UINT32      payload_size;
    UINT32      start_offset;
    UINT32      copy_index;
    UINT32      pid;
    sdesc   *curr_desc;
    sdesc   *head_desc;


    // Consistency check
    assert(slice_head);

    // Get descriptor
    sdesc *hw_desc = desc_get();

    // Set this as another chain
    sdesc *hw_desc_head = hw_desc;

    // Get a hw buffer
    sDECODER_HW_BUFFER * hw_buf = video_sink_buf_get();
    assert(hw_buf != NULL);

    // Set payload
    hw_desc->data = (UINT8 *) hw_buf;

    // Get first descriptor
    // First descriptor holds the timestamp and will be skipped
    curr_desc = slice_head;

    copy_index = 0;
    do
    {
        // Get next
        curr_desc = curr_desc->next;

        // Get current
        curr_ptr = curr_desc->data;

        // Get data left
        bytes_left = curr_desc->data_len;
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

            if(pid == 0x1011)
            {
                UINT8 stuffing = 0; 
                if(afc & 0x02)
                {
                    stuffing = 1 + ts->payload.payload[0]; 
                }

                start_offset = stuffing; 

                if(PUSI_GET(ts->hdr))
                {
                     start_offset += 14;
                }

                payload_size = sizeof(sMPEG2_TS_PAYLOAD) - start_offset;

                if((copy_index + payload_size) > 81920)
                {
                    // If the hw buffer is full, just submit the current buffer
                    hw_buf->buffer_len = copy_index;

                    // Get a new descriptor
                    hw_desc->next = desc_get();

                    // Point to the new descriptor
                    hw_desc = hw_desc->next;

                    // Get a new buffer
                    hw_buf = video_sink_buf_get();
                    assert(hw_buf != NULL);

                    // Set new payload
                    hw_desc->data = (UINT8 *) hw_buf;

                    // Reset index
                    copy_index = 0;
                }

                memcpy(&hw_buf->buffer[copy_index],
                        &ts->payload.payload[start_offset],
                        payload_size);

                copy_index += payload_size;
            }

            curr_ptr += sizeof(sMPEG2_TS);
            bytes_left -= sizeof(sMPEG2_TS);

        } while (bytes_left > 0);

    } while (curr_desc->next != NULL);

    // Set length
    hw_buf->buffer_len = copy_index;

    // Free the existing slice, minus the head (timestamp)
    desc_put(slice_head->next);

    // Set slice
    slice_head->next = hw_desc_head;

    pipe_put(VRDMA_SLICE_READY, slice_head);
}

static UINT64 sink_time_get(
    void
    )
{
    struct timeval  curr_time;


    // Get current time
    gettimeofday(&curr_time, NULL);

    UINT64 temp = curr_time.tv_sec * 1000 + curr_time.tv_usec / 1000;

    return temp;
}


// Get estimated source time
static UINT64 estimated_source_time_get(
    void
    )
{
    UINT64  time;


    pthread_mutex_lock(&f_cblk.lock);

    time = f_cblk.curr_time;

    pthread_mutex_unlock(&f_cblk.lock);

    return time;
}


void video_scheduler_thread(
    void * arg
    )
{
    sdesc   *desc;
    UINT64      slice_present_time;


    desc = NULL;
    while(1)
    {
        while(1)
        {
            // Get slice
            if(desc == NULL)
            {
                desc = pipe_get(VRDMA_SLICE_READY);
                if(desc == NULL)
                {
                    goto next_iter;
                }

                sSLICE_HDR *hdr = (sSLICE_HDR *) desc->data;

                // Get PTS
                slice_present_time = ((sSLICE_HDR *) desc->data)->timestamp;
            }

            UINT64  estimated_source_time = estimated_source_time_get();
            UINT8 present = (estimated_source_time > slice_present_time) ? 1 : 0;

            if(!present)
            {
                goto next_iter;
            }

            // Present the slice
            sdesc *curr = desc->next;
            sdesc *next;
            do
            {
                video_sink_buf_set((sDECODER_HW_BUFFER *) curr->data);

                next = curr->next;

                free(curr);

                curr = next;

            } while (curr != NULL);

            free(desc->data);
            free(desc);

            // Set descriptor pointer to NULL
            desc = NULL;
        }

        next_iter:

        usleep(1*1000);
    }
}


// Slice packing thread
void slice_packing_thread(
    void * arg
    )
{
    sdesc   *desc;


    desc = NULL;
    while(1)
    {
        UINT32 len = pipe_len_get(VRDMA_SLICE_READY);
        if(len >= 10)
        {
            // More than enough. Try again next iteration
            goto next_iter;
        }

        UINT32  slices_to_dump = 10 - len;
        do
        {
            desc = pipe_get(VRDMA_SLICE);
            if(desc == NULL)
            {
                goto next_iter;
            }

            // Dump slice
            video_scheduler_slice_dump(desc);

            slices_to_dump--;

        } while (slices_to_dump > 0);

        next_iter:

        usleep(2*1000);
    }
}


// Updates program clock reference time
void pcr_update_thread(
    void * arg
    )
{
    sdesc   *desc;
    UINT64      pcr_time;
    UINT64      pcr_received_time;
    UINT64      curr_time;


    while(1)
    {
        desc = pipe_get(VRDMA_PCR);
        if(desc == NULL)
        {
            goto cleanup;
        }

        sSLICE_HDR *hdr = (sSLICE_HDR *) desc->data;

        // Update PCR time
        pcr_time = hdr->timestamp;

        // Free descriptor
        desc_put(desc);

        // Cache received time
        pcr_received_time = sink_time_get();

        cleanup:

        // Get current time
        curr_time = sink_time_get();

        pthread_mutex_lock(&f_cblk.lock);

        f_cblk.curr_time = pcr_time + (curr_time - pcr_received_time) - system_AUDIO_SOURCE_DELAY_MS - system_DELAY_MS;

        pthread_mutex_unlock(&f_cblk.lock);

        usleep(2*1000);
    }
}


static void resources_init(
    void
    )
{
    // Initialize decoder hardware
    video_sink_init();

    pthread_mutex_init(&f_cblk.lock, NULL);
}


static void video_scheduler_thread_create(
    void
    )
{
    thread_create(&f_cblk.video_scheduler_thread,
                     &video_scheduler_thread,
                     NULL,
                     VIDEO_SCHEDULER_THREAD_PRIORITY);

    thread_create(&f_cblk.slice_packing_thread,
                     &slice_packing_thread,
                     NULL,
                     VIDEO_SCHEDULER_THREAD_PRIORITY);

    thread_create(&f_cblk.pcr_update_thread,
                     &pcr_update_thread,
                     NULL,
                     VIDEO_SCHEDULER_THREAD_PRIORITY);
}


void video_scheduler_init(
    void
    )
{
    // Initialize common resources
    resources_init();
}


void video_scheduler_open(
    void
    )
{
    // Initialize decoder thread
    video_scheduler_thread_create();
}


void video_scheduler_close(
    void
    )
{
}
