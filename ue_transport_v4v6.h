// File: ue_transport_v4v6.h
#pragma once

#include <stdint.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <sys/socket.h>

// IP version-agnostic address structure
typedef union {
    struct {
        uint32_t addr;
        uint8_t  padding[12];
    } v4;
    struct {
        uint8_t addr[16];
    } v6;
    uint8_t raw[16];  // Always 16 bytes for alignment
} ue_ip_addr_t;

// Enhanced UET header with version support
typedef struct {
    uint8_t version:4;       // UET version
    uint8_t ip_version:4;    // IP version (4 or 6)
    uint8_t flags;
    uint16_t length;
    uint32_t flow_id;
    uint32_t sequence_num;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed)) uet_header_v2_t;

// Dual-stack connection structure
struct ue_connection {
    uint32_t local_conn_id;
    uint32_t remote_conn_id;
    
    // IP version-agnostic addresses
    ue_ip_addr_t local_addr;
    ue_ip_addr_t remote_addr;
    uint16_t local_port;
    uint16_t remote_port;
    
    uint8_t ip_version;      // 4 or 6
    uint32_t state;
    uint64_t last_activity;
    
    // Connection pool management
    struct ue_conn_pool *pool;
    struct list_head pool_entry;
};

// Dual-stack packet structure
typedef struct {
    union {
        struct iphdr ipv4_hdr;
        struct ip6_hdr ipv6_hdr;
    } ip_hdr;
    struct udphdr udp_hdr;
    uet_header_v2_t uet_hdr;
    uint8_t payload[];
} __attribute__((packed)) ue_packet_v2_t;

// Address utility functions
static inline int ue_addr_family(const ue_ip_addr_t *addr, uint8_t ip_version)
{
    return (ip_version == 4) ? AF_INET : AF_INET6;
}

static inline size_t ue_addr_len(uint8_t ip_version)
{
    return (ip_version == 4) ? 4 : 16;
}

static inline void ue_addr_copy(ue_ip_addr_t *dst, const ue_ip_addr_t *src, 
                               uint8_t ip_version)
{
    if (ip_version == 4) {
        dst->v4.addr = src->v4.addr;
        memset(dst->v4.padding, 0, 12);
    } else {
        memcpy(dst->v6.addr, src->v6.addr, 16);
    }
}

static inline int ue_addr_compare(const ue_ip_addr_t *a, const ue_ip_addr_t *b,
                                 uint8_t ip_version)
{
    if (ip_version == 4) {
        return (a->v4.addr == b->v4.addr) ? 0 : 1;
    } else {
        return memcmp(a->v6.addr, b->v6.addr, 16);
    }
}

// Convert sockaddr to ue_ip_addr_t
static inline int ue_sockaddr_to_addr(const struct sockaddr *sa, 
                                     ue_ip_addr_t *addr, uint8_t *ip_version)
{
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
        *ip_version = 4;
        addr->v4.addr = sin->sin_addr.s_addr;
        memset(addr->v4.padding, 0, 12);
        return 0;
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
        *ip_version = 6;
        memcpy(addr->v6.addr, &sin6->sin6_addr, 16);
        return 0;
    }
    return -1;
}

// Convert ue_ip_addr_t to sockaddr
static inline int ue_addr_to_sockaddr(const ue_ip_addr_t *addr, uint8_t ip_version,
                                     uint16_t port, struct sockaddr_storage *ss)
{
    memset(ss, 0, sizeof(*ss));
    
    if (ip_version == 4) {
        struct sockaddr_in *sin = (struct sockaddr_in *)ss;
        sin->sin_family = AF_INET;
        sin->sin_addr.s_addr = addr->v4.addr;
        sin->sin_port = htons(port);
        return sizeof(struct sockaddr_in);
    } else if (ip_version == 6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ss;
        sin6->sin6_family = AF_INET6;
        memcpy(&sin6->sin6_addr, addr->v6.addr, 16);
        sin6->sin6_port = htons(port);
        return sizeof(struct sockaddr_in6);
    }
    return -1;
}

// Enhanced connection management with dual-stack support
struct ue_connection *ue_get_ephemeral_conn_v2(struct ue_ep *ep,
                                               const ue_ip_addr_t *remote_addr,
                                               uint16_t remote_port,
                                               uint8_t ip_version)
{
    struct ue_conn_pool *pool = &ep->conn_pool;
    struct ue_connection *conn;
    
    // Search for existing connection
    list_for_each_entry(conn, &pool->active_conns, pool_entry) {
        if (conn->ip_version == ip_version &&
            conn->remote_port == remote_port &&
            ue_addr_compare(&conn->remote_addr, remote_addr, ip_version) == 0 &&
            time_before(jiffies, conn->last_activity + UE_CONN_TIMEOUT)) {
            
            conn->last_activity = jiffies;
            return conn;
        }
    }
    
    return NULL;  // No suitable connection found
}

// Dual-stack RDMA operations
static int ue_rdma_write_immediate_v2(struct ue_ep *ep, const void *buf,
                                     size_t len, const ue_ip_addr_t *remote_addr,
                                     uint16_t remote_port, uint8_t ip_version,
                                     uint32_t rkey)
{
    struct ue_connection *conn;
    
    // Get ephemeral connection from pool
    conn = ue_get_ephemeral_conn_v2(ep, remote_addr, remote_port, ip_version);
    if (!conn) {
        // Create temporary connection state
        conn = ue_create_temp_connection_v2(ep, remote_addr, remote_port, ip_version);
        if (!conn)
            return -ENOMEM;
    }
    
    // Direct memory write without connection setup
    return ue_post_rdma_write_v2(conn, buf, len, rkey);
}

// Enhanced packet spraying with dual-stack support
struct ue_multipath_v2 {
    uint8_t num_paths;
    uint32_t entropy_seed;
    uint8_t ip_version;
    
    struct {
        ue_ip_addr_t next_hop;
        uint16_t weight;
        uint32_t packets_sent;
        uint32_t congestion_level;
        uint32_t rtt;
    } path_stats[UE_MAX_PATHS];
};

static int ue_setup_multipath_v2(struct ue_ep *ep, const ue_ip_addr_t *dest_addr,
                                 uint8_t ip_version)
{
    struct ue_multipath_v2 *mp = &ep->multipath_v2;
    
    mp->ip_version = ip_version;
    
    // Query available paths using ECMP/WCMP for the specific IP version
    if (ip_version == 4) {
        mp->num_paths = ue_query_ecmp_paths_v4(&dest_addr->v4.addr);
    } else {
        mp->num_paths = ue_query_ecmp_paths_v6(dest_addr->v6.addr);
    }
    
    mp->entropy_seed = rand();
    
    // Initialize load balancing state
    for (int i = 0; i < mp->num_paths; i++) {
        mp->path_stats[i].packets_sent = 0;
        mp->path_stats[i].congestion_level = 0;
        mp->path_stats[i].rtt = 0;
    }
    
    return 0;
}

// Enhanced libfabric provider with dual-stack support
struct ue_provider_v2 {
    struct fid_fabric fabric;
    struct fi_provider *prov;
    uint32_t version;
    
    // IP version capabilities
    uint64_t caps;
    uint64_t mode;
    uint32_t addr_format;  // FI_SOCKADDR_IN or FI_SOCKADDR_IN6 or both
    
    enum fi_threading threading;
    enum fi_progress control_progress;
    enum fi_progress data_progress;
};

// Dual-stack endpoint creation
static int ue_endpoint_create_v2(struct fid_domain *domain, struct fi_info *info,
                                struct fid_ep **ep, void *context)
{
    struct ue_ep *ue_ep;
    struct ue_domain *ue_domain = container_of(domain, struct ue_domain, domain_fid);
    
    ue_ep = calloc(1, sizeof(*ue_ep));
    if (!ue_ep)
        return -FI_ENOMEM;
    
    // Determine supported address formats
    switch (info->addr_format) {
        case FI_SOCKADDR_IN:
            ue_ep->supported_ip_versions = UE_IPV4_ONLY;
            break;
        case FI_SOCKADDR_IN6:
            ue_ep->supported_ip_versions = UE_IPV6_ONLY;
            break;
        case FI_SOCKADDR:
            ue_ep->supported_ip_versions = UE_IPV4_AND_IPV6;
            break;
        default:
            free(ue_ep);
            return -FI_EINVAL;
    }
    
    // Initialize dual-stack connection pools
    INIT_LIST_HEAD(&ue_ep->conn_pool.active_conns);
    ue_ep->conn_pool.max_conns = UE_MAX_CONNECTIONS;
    
    ue_ep->ep_fid.fid.fclass = FI_CLASS_EP;
    ue_ep->ep_fid.fid.context = context;
    ue_ep->ep_fid.fid.ops = &ue_ep_fi_ops;
    ue_ep->ep_fid.ops = &ue_ep_ops;
    ue_ep->ep_fid.cm = &ue_ep_cm_ops;
    ue_ep->ep_fid.msg = &ue_ep_msg_ops;
    ue_ep->ep_fid.rma = &ue_ep_rma_ops;
    
    *ep = &ue_ep->ep_fid;
    return 0;
}

// Enhanced send operation with automatic IP version detection
static ssize_t ue_send_v2(struct fid_ep *ep, const void *buf, size_t len,
                         void *desc, fi_addr_t dest_addr, void *context)
{
    struct ue_ep *ue_ep = container_of(ep, struct ue_ep, ep_fid);
    struct ue_tx_entry *tx_entry;
    struct sockaddr_storage ss;
    ue_ip_addr_t addr;
    uint8_t ip_version;
    uint16_t port;
    
    // Convert fi_addr_t
