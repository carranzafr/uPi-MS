#include <stdio.h>
#include <pthread.h>

#include "desc.h"
#include "udp.h"
#include "pkt.h"
#include "thread.h"
#include "pipe.h"

#include "data.h"


typedef struct
{
    pthread_t       pkt_rx_thread;  ///< Packet receive thread
    SBOX_UDP_ID     udp_sock;       ///< UDP socket
    unsigned int    rx_pkt;         ///< Number of received packet

} sdata_CBLK;


static sdata_CBLK  f_cblk;


static void pkt_rx_thread(
    void * arg
    )
{
    sPI_PORTAL_PKT *pkt;
    UINT16          last_seq_num;
    UINT16          curr_seq_num;


    while(1)
    {
        // Malloc packet to hold data
        pkt = malloc(sizeof(sPI_PORTAL_PKT));

        // Receive packet
        UINT32 pkt_len = sizeof(sPI_PORTAL_PKT);

        udp_recv(f_cblk.udp_sock, (char *) pkt, &pkt_len);

        sRTP_HDR *rtp_hdr = (sRTP_HDR *) pkt;

        curr_seq_num = ntohs(rtp_hdr->sequence_num);
        if((UINT16) (last_seq_num + 1) != curr_seq_num)
        {
            printf("(data): last_seq_num = %u, curr_seq_num = %u, lost %u pkts.\n",
                    last_seq_num,
                    curr_seq_num,
                    curr_seq_num - (last_seq_num + 1));
        }

        // Cache last sequence number
        last_seq_num = curr_seq_num;

        sdesc *desc = desc_get();

        desc->data      = (void *) pkt;
        desc->data_len  = pkt_len;

        pipe_put(VRDMA_PKT_QUEUE, desc);

        f_cblk.rx_pkt++;
    }
}


static void pkt_rx_thread_create(
    void
    )
{
    thread_create(&f_cblk.pkt_rx_thread, &pkt_rx_thread, NULL, data_RX_THREAD_PRIORITY);
}


void data_init(
    void
    )
{
    f_cblk.udp_sock = udp_create(50000);

    printf("(data): data_init(): Initialized.\n");
}


void data_open(
    void
    )
{
    pkt_rx_thread_create();

    printf("(data_open): Opened.\n");
}
