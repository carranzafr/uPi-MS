#if !defined(_VRDMA_H_)
#define _VRDMA_H_

#define VRDMA_PKT_QUEUE          0
#define VRDMA_VIDEO_PKT_QUEUE    1
#define VRDMA_SLICE          	2
#define VRDMA_SLICE_READY    	3
#define VRDMA_PCR            	4
#define VRDMA_LPCM           	5
#define VRDMA_LPCM_SLICE     	6
#define VRDMA_MAX            	7

extern void pipe_init(
    void
    );

extern void pipe_put(
    UINT32  index,
    void   *data
    );

extern void * pipe_get(
    UINT32  index
    );

extern unsigned int pipe_len_get(
    UINT32  index
    );

#endif // _VRDMA_H_
