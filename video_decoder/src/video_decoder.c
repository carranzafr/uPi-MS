#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <mqueue.h>
#include <assert.h>
#include <errno.h>
#include <sys/time.h>
#include <netinet/in.h>

#include "pkt.h"
#include "desc.h"
#include "pipe.h"
#include "thread_priority.h"
#include "video_sink.h"

typedef struct
{
    sdesc   *head;
    sdesc   *tail;

} sSLICE_CHAIN;

// Decoder control block
typedef struct
{
    pthread_t       decoder_thread;
    UINT32          look_for_new_slice;
    UINT32          pes_payload_curr_len;
    UINT32          last_cc;
    UINT32          slice_pkt_count;
    UINT64          slice_pts_ms;
    UINT64          slice_count;
    UINT16          last_seq_num;
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


// Dumps a slice onto the queue
static void video_decoder_slice_dump(
    UINT64      pts_ms,
    sdesc   *slice_head
    )
{
    sSLICE_HDR *hdr = malloc(sizeof(sSLICE_HDR));
    assert(hdr != NULL);

    hdr->type       = SLICE_TYPE_SLICE;
    hdr->timestamp  = pts_ms;

    sdesc   *new_desc = desc_get();

    new_desc->data = (void *) hdr;
    new_desc->data_len = sizeof(sSLICE_HDR);

    new_desc->next = slice_head;

    pipe_put(VRDMA_SLICE, new_desc);
}


// Drops a given slice
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


// Add a packet to a slice chain
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


// Interpret mpeg 2 transport stream data
static void m2ts_data_interpret(
    sdesc   *desc,
    UINT32     *pes_payload_size,
    UINT8      *cc_start,
    UINT8      *cc_end
    )
{
    UINT8  *curr_ptr;
    UINT32  bytes_left;
    UINT32  afc;
    UINT32  pid;
    UINT32  pes_byte_count; 
    UINT32  payload_size;
    UINT8   cc;


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

    sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;

    UINT8   first_chunk = 1;

    pes_byte_count = 0; 
    do
    {
        sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;
        afc = AFC_GET(ts->hdr);
        pid = PID_GET(ts->hdr);

        if(pid == 0x1011)
        {
            cc = CC_GET(ts->hdr);
            if(first_chunk)
            {
                *cc_start = cc;

                first_chunk = 0;
            }

            if(PUSI_GET(ts->hdr))
            {
                payload_size = (sizeof(sMPEG2_TS_PAYLOAD) - 14);
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

        bytes_left -= sizeof(sMPEG2_TS);
        curr_ptr += sizeof(sMPEG2_TS);

    } while (bytes_left > 0); 

    *cc_end = cc;
    *pes_payload_size = pes_byte_count;
}


// Find the slice start
static UINT8 slice_start_find(
    sdesc   *desc,
    UINT32     *pes_payload_size,
    UINT64     *pts_ms
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

        if(PUSI_GET(ts->hdr) && (pid == 0x1011))
        {
            curr_ptr = &ts->payload.payload[0];

            UINT8 adaptation_field_len = 0; 
            if(afc & 0x02)
            {
                adaptation_field_len = 1 + *curr_ptr; 
            }

            // Skip 'adaptation field length' field
            curr_ptr += adaptation_field_len;

            // Skip adaptation field length
            curr_ptr += sizeof(sPES);

            sPES_EXT *pes_ext = (sPES_EXT *) curr_ptr;

            UINT16  len = ntohs(pes_ext->length);
            if(len > 0)
            {
                *pes_payload_size = len - 8;
            }
            else
            {
                *pes_payload_size = 0;
            }

            curr_ptr += sizeof(sPES_EXT);

            if((*curr_ptr != 0x05) && (*curr_ptr != 0x0a))
            {
                printf("0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", 
                       ((UINT8 *) ts)[0], 
                       ((UINT8 *) ts)[1], 
                       ((UINT8 *) ts)[2], 
                       ((UINT8 *) ts)[3], 
                       ((UINT8 *) ts)[4], 
                       ((UINT8 *) ts)[5], 
                       ((UINT8 *) ts)[6], 
                       ((UINT8 *) ts)[7]); 

                printf("0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", 
                       ((UINT8 *) ts)[8], 
                       ((UINT8 *) ts)[9], 
                       ((UINT8 *) ts)[10], 
                       ((UINT8 *) ts)[11], 
                       ((UINT8 *) ts)[12], 
                       ((UINT8 *) ts)[13], 
                       ((UINT8 *) ts)[14], 
                       ((UINT8 *) ts)[15]); 

                printf("*curr_ptr = 0x%x, delta = %u\n",
                       *curr_ptr, 
                       (UINT8 *)curr_ptr - (UINT8 *) ts);

                assert(0); 
            }

            curr_ptr++;

            UINT32  i;
            UINT64  pts;
            pts = 0;
            for(i = 0; i < 5; i++)
            {
                pts = ((pts << 8) | curr_ptr[i]);
            }

            UINT64 pts_hz;
            UINT64 mask;

            pts_hz = 0;

            mask = 0x0007;
            pts_hz |= (pts & (mask << 33)) >> 3;

            mask = 0x7fff;
            pts_hz |= (pts & (mask << 17)) >> 2;

            mask = 0x7fff;
            pts_hz |= (pts & (mask << 1)) >> 1;

            // Convert to ms
            *pts_ms = pts_hz / 90;

            return 1;
        }

        curr_ptr += sizeof(sMPEG2_TS);
        bytes_left -= sizeof(sMPEG2_TS);

    } while (bytes_left > 0);

    return 0;
}


void video_decoder_thread(
    void * arg
    )
{
    sdesc  *desc;
    sdesc  *slice_start;
    sdesc  *slice_end;
    UINT8      *curr_ptr;
    UINT32      bytes_left;
    sMPEG2_TS  *ts;
    UINT32      pes_payload_len;
    UINT8       slice_start_found;
    UINT8       cc_start;
    UINT8       cc_end;
    UINT32      pes_payload_size;


    while(1)
    {
        do
        {
            desc = pipe_get(VRDMA_VIDEO_PKT_QUEUE);
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

            // Get TS header
            curr_ptr += sizeof(sRTP_HDR);
            ts = (sMPEG2_TS *) curr_ptr;

            // Always Look for slice start
            UINT64  pts_ms;
            slice_start_found = slice_start_find(desc, &pes_payload_len, &pts_ms);

            if(f_cblk.look_for_new_slice)
            {
                if(!slice_start_found)
                {
                    goto cleanup;
                }

                // Found new slice. Keep going
                f_cblk.look_for_new_slice = 0;
            }

            if(slice_start_found)
            {
                if(f_cblk.slice_count == 0)
                {
                    f_cblk.slice_pts_ms = pts_ms;
                }

                if(f_cblk.slice_pkt_count > 0)
                {
                    video_decoder_slice_dump(f_cblk.slice_pts_ms, slice_get());

                    // Reset slice packet count
                    f_cblk.slice_pkt_count      = 0;
                    f_cblk.pes_payload_curr_len = 0;
                    f_cblk.slice_pts_ms         = pts_ms;
                }

                f_cblk.slice_count++;
            }

            // Interpret packet
            m2ts_data_interpret(desc, &pes_payload_size, &cc_start, &cc_end);

            f_cblk.pes_payload_curr_len += pes_payload_size;

            if(f_cblk.slice_pkt_count == 0)
            {
                // If first packet, just cache the last CC
                f_cblk.last_cc = cc_end;
            }
            else
            {
                // If not first packet, check for continuity
                if(((f_cblk.last_cc + 1) & 0x0F) == cc_start)
                {
                    // Passes continuity test
                    f_cblk.last_cc = cc_end;
                }
                else
                {
                    // Fails continuity test
                    printf("(video_decoder): last rtp seq num = %d\n", f_cblk.last_seq_num);
                    printf("(video_decoder): curr rtp seq num = %d\n", ntohs(rtp_hdr->sequence_num));

                    printf("(video_decoder): Packet loss detected! last_cc = %u, start_cc = %d\n",
                            f_cblk.last_cc, cc_start);

                    slice_drop();

                    f_cblk.look_for_new_slice = 1;

                    f_cblk.slice_pkt_count = 0;

                    goto cleanup;
                }
            }

            slice_pkt_add(desc);

            f_cblk.slice_pkt_count++;

            cleanup:

            f_cblk.last_seq_num = ntohs(rtp_hdr->sequence_num);

        } while (1);

        usleep(8*1000);
    }
}


static void resources_init(
    void
    )
{
    f_cblk.look_for_new_slice = 1;
}


static void decoder_thread_create(
    void
    )
{
    thread_create(&f_cblk.decoder_thread, &video_decoder_thread, NULL, VIDEO_DECODER_THREAD_PRIORITY);
}


void video_decoder_init(
    void
    )
{
    // Initialize common resources
    resources_init();
}


void video_decoder_open(
    void
    )
{
    // Initialize decoder thread
    decoder_thread_create();
}


void video_decoder_close(
    void
    )
{
}
