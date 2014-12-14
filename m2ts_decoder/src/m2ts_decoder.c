#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <mqueue.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>

#include "udp.h"
#include "pkt.h"
#include "desc.h"
#include "thread.h"
#include "queue.h"

#include "types.h"
#include "pipe.h"


typedef struct
{
    pthread_t   pkt_process_thread;

} sdata_CBLK;


static sdata_CBLK  f_cblk;

typedef enum
{
    PKT_TYPE_AUDIO,
    PKT_TYPE_VIDEO,
    PKT_TYPE_NULL

} ePKT_TYPE;


static UINT64 pcr_get(
    sdesc   *desc
    )
{
    UINT8  *curr_ptr;
    UINT32  bytes_left;

    static UINT64   last_pcr_ms;

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

    do
    {
        sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;
        UINT16 pid = PID_GET(ts->hdr);

        if(pid == 0x1000)
        {
            curr_ptr += (sizeof(sMPEG2_TS_HDR) + 2);

            UINT64  pcr = 0;
            UINT32  i;
            for(i = 0; i < 6; i++)
            {
                pcr = ((pcr << 8) | curr_ptr[i]);
            }

            UINT64 pcr_base = (pcr >> (9 + 6));

            UINT64 pcr_ext = pcr & (0x1FF);

            pcr = pcr_base * 300 + pcr_ext;

            UINT64  pcr_ms = pcr / 27000;

            last_pcr_ms = pcr_ms;

            return pcr_ms;
        }

        bytes_left -= sizeof(sMPEG2_TS);
        curr_ptr += sizeof(sMPEG2_TS);

    } while (bytes_left > 0);

    return 0;
}


static ePKT_TYPE pkt_type_get(
    sdesc   *desc
    )
{
    UINT8  *curr_ptr;
    UINT32  bytes_left;

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

    do
    {
        sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;
        UINT16 pid = PID_GET(ts->hdr);

#if 0

#endif

        if(pid == 0x1011)
        {
            return PKT_TYPE_VIDEO;
        }
        else if(pid == 0x1100)
        {
            return PKT_TYPE_AUDIO;
        }

        bytes_left -= sizeof(sMPEG2_TS);
        curr_ptr += sizeof(sMPEG2_TS);

    } while (bytes_left > 0);

    return PKT_TYPE_NULL;
}


static void pkt_process_thread(
    void * arg
    )
{
    queue   *queue;
    sdesc   *desc;
    sdesc   *h264_desc;
    sdesc   *lpcm_desc;
    UINT32      bytes_left;


    while(1)
    {
        do
        {
            desc = pipe_get(VRDMA_PKT_QUEUE);
            if(desc == NULL)
            {
                break;
            }

            // Get data left
            bytes_left = desc->data_len;
            assert(bytes_left > sizeof(sRTP_HDR));

            UINT64 pcr_ms = pcr_get(desc);
            if(pcr_ms > 0)
            {
                sdesc *new_desc = desc_get();

                sSLICE_HDR *hdr = malloc(sizeof(sSLICE_HDR));
                assert(hdr != NULL);

                hdr->type       = SLICE_TYPE_PCR;
                hdr->timestamp  = pcr_ms;

                new_desc->data = (void *) hdr;
                new_desc->data_len = sizeof(sSLICE_HDR);

                pipe_put(VRDMA_PCR, new_desc);
            }

            ePKT_TYPE pkt_type = pkt_type_get(desc);
            switch(pkt_type)
            {
                case PKT_TYPE_VIDEO:
                {
                    pipe_put(VRDMA_VIDEO_PKT_QUEUE, desc);
                    break;
                }
                case PKT_TYPE_AUDIO:
                {
                    pipe_put(VRDMA_LPCM, desc);
                    break;
                }
                case PKT_TYPE_NULL: 
                {
                    desc_put(desc);
                    break;
                }
                default:
                {
                    assert(0);
                    break;
                }
            }

        } while(1);

        usleep(1*1000);
    }
}


static void pkt_process_thread_create(
    void
    )
{
    thread_create(&f_cblk.pkt_process_thread, &pkt_process_thread, NULL, m2ts_PKT_PROCESS_THREAD_PRIORITY);
}


void m2ts_decoder_init(
    void
    )
{
    printf("(m2ts): m2ts_decoder_init(): Initialized.\n");
}


void m2ts_decoder_open(
    void
    )
{
    pkt_process_thread_create();

    printf("(m2ts_decoder_open): m2ts_decoder_open(): Invoked.\n");
}
