#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <mqueue.h>
#include <assert.h>
#include <errno.h>
#include <sys/time.h>

#include "pkt.h"
#include "desc.h"
#include "pipe.h"
#include "thread_priority.h"
#include "audio_sink.h"


typedef struct
{
    sdesc   *head;
    sdesc   *tail;

} sSLICE_CHAIN;


typedef struct
{
    pthread_t       decoder_thread;
    UINT32          look_for_new_slice;
    UINT32          continue_current_slice;
    UINT32          pes_len;
    UINT32          pes_curr_byte_count;
    UINT32          last_seq_num;
    sSLICE_CHAIN    slice_chain;

} sDECODER_CBLK;


// Decoder control block
static sDECODER_CBLK f_cblk;

static sdesc * slice_get(
    void
    )
{
    sdesc *head;


    head = f_cblk.slice_chain.head;

    f_cblk.slice_chain.head = NULL;
    f_cblk.slice_chain.tail = NULL;

    return head;
}


static void slice_drop(
    void
    )
{
    sdesc  *curr;
    sdesc  *next;


    // Drop slice
    desc_put(f_cblk.slice_chain.head);

    // Reset head and tail
    f_cblk.slice_chain.head = NULL;
    f_cblk.slice_chain.tail = NULL;

    printf("slice_dropped() invoked\n");
}


static void audio_decoder_slice_dump(
    sdesc   *slice_head
    )
{
    pipe_put(VRDMA_LPCM_SLICE, slice_head);
}


static UINT32 pes_payload_size(
    sdesc  *desc
    )
{
    UINT8  *curr_ptr;
    UINT32  bytes_left;
    UINT32  afc;
    UINT32  pid;
    UINT32  pes_byte_count;
    UINT32  payload_size;


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
            if(PUSI_GET(ts->hdr))
            {
                assert((afc == 0x01) || (afc == 0x03));

                payload_size = (sizeof(sMPEG2_TS_PAYLOAD) - 20);
            }
            else
            {
                if(afc == 0x01)
                {
                    payload_size = sizeof(sMPEG2_TS_PAYLOAD);
                }
                else if(afc == 0x03)
                {
                    payload_size = sizeof(sMPEG2_TS_PAYLOAD) - 1 - ts->payload.payload[0];
                }
                else
                {
                    assert(0);
                }
            }

            pes_byte_count += payload_size;
        }

        curr_ptr += sizeof(sMPEG2_TS);
        bytes_left -= sizeof(sMPEG2_TS);

    } while (bytes_left > 0);

    return pes_byte_count;
}


static void slice_pkt_add(
    sdesc   *desc
    )
{
    assert(desc != NULL);


    if(f_cblk.slice_chain.head == NULL)
    {
        f_cblk.slice_chain.head = desc;
        f_cblk.slice_chain.tail = desc;

        return;
    }

    // Append to tail
    f_cblk.slice_chain.tail->next = desc;

    // Update tail
    f_cblk.slice_chain.tail = desc;
}


static UINT8 slice_start_find(
    sdesc   *desc,
    UINT32     *pes_payload_size
    )
{
    UINT8      *curr_ptr;
    UINT32      bytes_left;
    sMPEG2_TS  *ts;
    UINT32      pid;
    UINT32      afc;


    // Get current
    curr_ptr = desc->data;

    // Get data left
    bytes_left = desc->data_len;
    assert(bytes_left > sizeof(sRTP_HDR));

    // Get RTP header
    sRTP_HDR *rtp_hdr = (sRTP_HDR *) curr_ptr;

    // Get TS header
    curr_ptr += sizeof(sRTP_HDR);

    // Get TS bytes left
    bytes_left -= sizeof(sRTP_HDR);
    assert((bytes_left % sizeof(sMPEG2_TS)) == 0);

    do
    {
        sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;

        afc = AFC_GET(ts->hdr);
        pid = PID_GET(ts->hdr);

        if(PUSI_GET(ts->hdr) && (pid == 0x1100))
        {
            curr_ptr = &ts->payload.payload[0];

            curr_ptr += sizeof(sPES);

            sPES_EXT *pes_ext = (sPES_EXT *) curr_ptr;

            *pes_payload_size = ntohs(pes_ext->length) - 14;

            assert((*pes_payload_size == 1920) || (*pes_payload_size == -14));

            return 1;
        }

        curr_ptr += sizeof(sMPEG2_TS);
        bytes_left -= sizeof(sMPEG2_TS);

    } while (bytes_left > 0);

    return 0;
}


void audio_decoder_thread(
    void * arg
    )
{
    sdesc  *desc;
    UINT8      *curr_ptr;
    UINT32      bytes_left;
    sMPEG2_TS  *ts;

    UINT8   eos;


    while(1)
    {
        do
        {
            desc = pipe_get(VRDMA_LPCM);
            if(desc == NULL)
            {
                break;
            }

            // Get current
            curr_ptr = desc->data;

            // Get data left
            bytes_left = desc->data_len;
            assert(bytes_left > sizeof(sRTP_HDR));

            // Get RTP header
            sRTP_HDR *rtp_hdr = (sRTP_HDR *) curr_ptr;

            if(f_cblk.look_for_new_slice)
            {
                if(slice_start_find(desc, &f_cblk.pes_len))
                {
                    f_cblk.look_for_new_slice = 0;

                    f_cblk.continue_current_slice = 1;

                    f_cblk.pes_curr_byte_count = 0;

                    f_cblk.last_seq_num = ntohs(rtp_hdr->sequence_num) - 1;
                }
            }

            if(f_cblk.continue_current_slice)
            {
                if(ntohs(rtp_hdr->sequence_num) != (f_cblk.last_seq_num + 1))
                {
                    slice_drop();

                    f_cblk.continue_current_slice = 0;
                    f_cblk.look_for_new_slice = 1;
                }
                else
                {
                    f_cblk.last_seq_num = ntohs(rtp_hdr->sequence_num);

                    UINT32 pes_size = pes_payload_size(desc);

                    f_cblk.pes_curr_byte_count += pes_size;

                    assert(f_cblk.pes_curr_byte_count <= f_cblk.pes_len);

                    slice_pkt_add(desc);

                    if(f_cblk.pes_curr_byte_count == f_cblk.pes_len)
                    {
                        f_cblk.continue_current_slice = 0;
                        f_cblk.look_for_new_slice = 1;

                        audio_decoder_slice_dump(slice_get());
                    }
                }
            }

        } while (1);

        usleep(1000);
    }
}


static void resources_init(
    void
    )
{
    f_cblk.look_for_new_slice = 1;

    // Initialize decoder hardware
    audio_sink_init();
}


static void decoder_thread_create(
    void
    )
{
    thread_create(&f_cblk.decoder_thread, &audio_decoder_thread, NULL, audio_DECODER_THREAD_PRIORITY);
}


void audio_decoder_init(
    void
    )
{
    // Initialize common resources
    resources_init();
}


void audio_decoder_open(
    void
    )
{
    // Initialize decoder thread
    decoder_thread_create();
}


void audio_decoder_close(
    void
    )
{
}
