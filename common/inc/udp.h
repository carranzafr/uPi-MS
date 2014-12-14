#if !defined(_UDP_SOCKET_H_)
#define _UDP_SOCKET_H_

#define SBOX_UDP_ID void *

extern void * udp_create(
    unsigned short  local_port
    );

extern void udp_send(
    SBOX_UDP_ID     id,
    unsigned char  *pkt,
    unsigned int    pkt_len
    );

extern void udp_recv(
    SBOX_UDP_ID     id,
    char           *pkt,
    unsigned int   *pkt_len
    );

#endif // #if !defined(_UDP_SOCKET_H_)
