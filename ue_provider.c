// File: ue_provider.c
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>

// UET-specific provider structure
struct ue_provider {
    struct fid_fabric fabric;
    struct fi_provider *prov;
    uint32_t version;
    
    // UET-specific capabilities
    uint64_t caps;
    uint64_t mode;
    enum fi_threading threading;
    enum fi_progress control_progress;
    enum fi_progress data_progress;
};

// Deferrable Send implementation
static ssize_t ue_send_defer(struct fid_ep *ep, const void *buf,
                             size_t len, void *desc, fi_addr_t dest_addr,
                             void *context)
{
    struct ue_ep *ue_ep = container_of(ep, struct ue_ep, ep_fid);
    struct ue_tx_entry *tx_entry;
    
    // Optimistic send - assume buffer available at destination
    tx_entry = ue_alloc_tx_entry(ue_ep);
    if (!tx_entry)
        return -FI_EAGAIN;
    
    tx_entry->type = UE_OP_SEND_DEFER;
    tx_entry->buf = buf;
    tx_entry->len = len;
    tx_entry->dest_addr = dest_addr;
    tx_entry->context = context;
    
    // Skip rendezvous protocol for performance
    return ue_post_send_immediate(ue_ep, tx_entry);
}

// Multi-path packet spraying
static int ue_setup_multipath(struct ue_ep *ep, fi_addr_t dest_addr)
{
    struct ue_multipath *mp = &ep->multipath;
    
    // Query available paths using ECMP/WCMP
    mp->num_paths = ue_query_ecmp_paths(dest_addr);
    mp->entropy_seed = rand();
    
    // Initialize load balancing state
    for (int i = 0; i < mp->num_paths; i++) {
        mp->path_stats[i].packets_sent = 0;
        mp->path_stats[i].congestion_level = 0;
        mp->path_stats[i].rtt = 0;
    }
    
    return 0;
}
