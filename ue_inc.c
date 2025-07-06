// File: ue_inc.c
#include "ue_transport.h"

// INC operation types
enum ue_inc_op {
    UE_INC_ALLREDUCE,
    UE_INC_ALLGATHER,
    UE_INC_REDUCE_SCATTER,
    UE_INC_BROADCAST
};

struct ue_inc_request {
    enum ue_inc_op operation;
    uint32_t group_id;
    uint32_t rank;
    uint32_t root_rank;
    void *send_buf;
    void *recv_buf;
    size_t count;
    enum ue_datatype datatype;
    enum ue_inc_reduce_op reduce_op;
};

// Switch-accelerated collective operations
static int ue_inc_allreduce_offload(struct ue_ep *ep,
                                    struct ue_inc_request *req)
{
    struct ue_inc_context *inc_ctx;
    struct ue_packet *control_pkt;
    
    // Create INC context
    inc_ctx = ue_alloc_inc_context(ep, req);
    if (!inc_ctx)
        return -ENOMEM;
    
    // Send control packet to switch for offload setup
    control_pkt = ue_create_inc_control_packet(req);
    ue_send_to_switch(ep, control_pkt, UE_INC_SETUP);
    
    // Post data buffers for switch processing
    return ue_post_inc_buffers(inc_ctx, req);
}

// Libfabric INC extension
int fi_inc_allreduce(struct fid_ep *ep, const void *sendbuf, void *recvbuf,
                     size_t count, enum fi_datatype datatype,
                     enum fi_op op, uint64_t group_id, void *context)
{
    struct ue_ep *ue_ep = container_of(ep, struct ue_ep, ep_fid);
    struct ue_inc_request req = {
        .operation = UE_INC_ALLREDUCE,
        .group_id = group_id,
        .send_buf = (void*)sendbuf,
        .recv_buf = recvbuf,
        .count = count,
        .datatype = datatype,
        .reduce_op = op
    };
    
    return ue_inc_allreduce_offload(ue_ep, &req);
}
