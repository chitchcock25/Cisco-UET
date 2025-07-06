// File: ue_transport.h
#pragma once

#include <stdint.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

// UET Header Structure
typedef struct {
    uint8_t version:4;
    uint8_t reserved:4;
    uint8_t flags;
    uint16_t length;
    uint32_t flow_id;
    uint32_t sequence_num;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed)) uet_header_t;

// Packet Delivery Sub-layer (PDS)
typedef struct {
    uint8_t pds_type;
    uint8_t reliability_mode;
    uint16_t connection_id;
    uint32_t ack_num;
    uint16_t window_size;
    uint16_t options;
} __attribute__((packed)) pds_header_t;

// Semantic Sub-layer
typedef struct {
    uint8_t op_code;
    uint8_t msg_type;
    uint16_t tag;
    uint64_t remote_addr;
    uint32_t rkey;
    uint32_t length;
} __attribute__((packed)) semantic_header_t;

// UET packet structure
typedef struct {
    struct iphdr ip_hdr;
    struct udphdr udp_hdr;
    uet_header_t uet_hdr;
    pds_header_t pds_hdr;
    semantic_header_t sem_hdr;
    uint8_t payload[];
} __attribute__((packed)) uet_packet_t;
