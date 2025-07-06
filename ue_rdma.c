// File: ue_rdma.c
#include "ue_transport.h"

struct ue_connection {
    uint32_t local_id;
    uint32_t remote_id;
    uint64_t local_addr;
    uint64_t remote_addr;
    uint32_t state;
    uint64_t last_activity;
    
    // Ephemeral connection pool
    struct ue_conn_pool *pool;
    struct list_head pool_entry;
};

// Connectionless RDMA operations
static int ue_rdma_write_immediate(struct ue_ep *ep, const void *buf,
                                   size_t len, uint64_t remote_addr,
                                   uint32_t rkey)
{
    struct ue_connection *conn;
    
    // Get ephemeral connection from pool
    conn = ue_get_ephemeral_conn(ep, remote_addr);
    if (!conn) {
        // Create temporary connection state
        conn = ue_create_temp_connection(ep, remote_addr);
    }
    
    // Direct memory write without connection setup
    return ue_post_rdma_write(conn, buf, len, remote_addr, rkey);
}

// Connection pool management
static struct ue_connection *ue_get_ephemeral_conn(struct ue_ep *ep,
                                                   uint64_t remote_addr)
{
    struct ue_conn_pool *pool = &ep->conn_pool;
    struct ue_connection *conn;
    
    // Search for existing connection
    list_for_each_entry(conn, &pool->active_conns, pool_entry) {
        if (conn->remote_addr == remote_addr && 
            time_before(jiffies, conn->last_activity + UE_CONN_TIMEOUT)) {
            conn->last_activity = jiffies;
            return conn;
        }
    }
    
    return NULL;  // No suitable connection found
}
