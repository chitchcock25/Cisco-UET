// File: ue_forwarding.p4
#include <core.p4>
#include <v1model.p4>

// UET header definitions
header ethernet_h {
    bit<48> dst_addr;
    bit<48> src_addr;
    bit<16> ether_type;
}

header ipv4_h {
    bit<4>  version;
    bit<4>  ihl;
    bit<8>  diffserv;
    bit<16> total_len;
    bit<16> identification;
    bit<3>  flags;
    bit<13> frag_offset;
    bit<8>  ttl;
    bit<8>  protocol;
    bit<16> hdr_checksum;
    bit<32> src_addr;
    bit<32> dst_addr;
}

header udp_h {
    bit<16> src_port;
    bit<16> dst_port;
    bit<16> length;
    bit<16> checksum;
}

header uet_h {
    bit<4>  version;
    bit<4>  reserved;
    bit<8>  flags;
    bit<16> length;
    bit<32> flow_id;
    bit<32> sequence_num;
    bit<16> checksum;
    bit<16> urgent_ptr;
}

// Packet structure
struct headers {
    ethernet_h ethernet;
    ipv4_h     ipv4;
    udp_h      udp;
    uet_h      uet;
}

struct metadata {
    bit<32> entropy_hash;
    bit<16> ecmp_group_id;
    bit<8>  path_count;
    bit<8>  selected_path;
    bit<16> congestion_level;
}

// Parser implementation
parser UEParser(packet_in packet,
                out headers hdr,
                inout metadata meta,
                inout standard_metadata_t standard_metadata) {
    
    state start {
        transition parse_ethernet;
    }
    
    state parse_ethernet {
        packet.extract(hdr.ethernet);
        transition select(hdr.ethernet.ether_type) {
            0x0800: parse_ipv4;
            default: accept;
        }
    }
    
    state parse_ipv4 {
        packet.extract(hdr.ipv4);
        transition select(hdr.ipv4.protocol) {
            17: parse_udp;
            default: accept;
        }
    }
    
    state parse_udp {
        packet.extract(hdr.udp);
        transition select(hdr.udp.dst_port) {
            UE_UDP_PORT: parse_uet;
            default: accept;
        }
    }
    
    state parse_uet {
        packet.extract(hdr.uet);
        transition accept;
    }
}

// Ingress processing
control UEIngress(inout headers hdr,
                  inout metadata meta,
                  inout standard_metadata_t standard_metadata) {
    
    // Packet spraying action
    action spray_packet(bit<32> entropy_seed, bit<8> path_count) {
        // Generate entropy hash for load balancing
        hash(meta.entropy_hash,
             HashAlgorithm.crc32,
             entropy_seed,
             {hdr.ipv4.src_addr,
              hdr.ipv4.dst_addr,
              hdr.udp.src_port,
              hdr.udp.dst_port,
              hdr.uet.flow_id,
              hdr.uet.sequence_num},
             0xFFFFFFFF);
        
        // Select path based on entropy
        meta.selected_path = (bit<8>)(meta.entropy_hash % (bit<32>)path_count);
    }
    
    // ECMP group table
    table ecmp_group {
        key = {
            hdr.ipv4.dst_addr: lpm;
        }
        actions = {
            spray_packet;
            NoAction;
        }
        default_action = NoAction();
    }
    
    // Path selection table
    table path_select {
        key = {
            meta.ecmp_group_id: exact;
            meta.selected_path: exact;
        }
        actions = {
            set_nhop;
            drop;
        }
        default_action = drop();
    }
    
    action set_nhop(bit<48> nhop_dmac, bit<32> nhop_ipv4, bit<9> port) {
        hdr.ethernet.dst_addr = nhop_dmac;
        hdr.ipv4.dst_addr = nhop_ipv4;
        standard_metadata.egress_spec = port;
        hdr.ipv4.ttl = hdr.ipv4.ttl - 1;
    }
    
    // Congestion control
    action mark_ecn() {
        hdr.ipv4.diffserv = hdr.ipv4.diffserv | 0x03;  // Set ECN bits
    }
    
    table congestion_control {
        key = {
            standard_metadata.egress_spec: exact;
            standard_metadata.deq_qdepth: range;
        }
        actions = {
            mark_ecn;
            NoAction;
        }
        default_action = NoAction();
    }
    
    apply {
        if (hdr.ipv4.isValid()) {
            ecmp_group.apply();
            path_select.apply();
            congestion_control.apply();
        }
    }
}
