#include "ue_flow_manager.h"
#include "logger.h"
#include "tokenize.h"
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <random>
#include <chrono>

UEFlowManager::UEFlowManager(DBConnector *config_db, 
                            DBConnector *appl_db,
                            DBConnector *state_db) :
    m_config_db(config_db),
    m_appl_db(appl_db),
    m_state_db(state_db),
    m_config_consumer(config_db, CFG_UE_TRANSPORT_TABLE_NAME),
    m_flow_consumer(config_db, CFG_UE_FLOW_TABLE_NAME),
    m_default_flow_mode(UEFlowMode::RELIABLE_UNORDERED_DELIVERY),
    m_congestion_algorithm(UECongestionAlgorithm::UE_CUBIC_PLUS),
    m_default_window_size(65536),
    m_max_flows(1000000),
    m_flow_timeout_sec(300)
{
    SWSS_LOG_ENTER();
    SWSS_LOG_NOTICE("Ultra Ethernet Flow Manager initialized with max_flows=%d", m_max_flows);
}

void UEFlowManager::doTask(Consumer &consumer) {
    SWSS_LOG_ENTER();
    
    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end()) {
        KeyOpFieldsValuesTuple t = it->second;
        
        std::string key = kfvKey(t);
        std::string op = kfvOp(t);
        auto values = kfvFieldsValues(t);
        
        SWSS_LOG_DEBUG("Processing flow task: key=%s, op=%s", key.c_str(), op.c_str());
        
        try {
            if (&consumer == &m_config_consumer) {
                processTransportConfig(key, op, values);
            } else if (&consumer == &m_flow_consumer) {
                processFlowConfig(key, op, values);
            }
        } catch (const std::exception &e) {
            SWSS_LOG_ERROR("Exception processing flow task: %s", e.what());
        }
        
        it = consumer.m_toSync.erase(it);
    }
}

void UEFlowManager::processTransportConfig(const std::string &key, 
                                          const std::string &op,
                                          const std::vector<FieldValueTuple> &values) {
    SWSS_LOG_ENTER();
    
    if (key != "global") {
        SWSS_LOG_WARN("Unknown transport config key: %s", key.c_str());
        return;
    }
    
    if (op == SET_COMMAND) {
        for (auto &fv : values) {
            std::string field = fvField(fv);
            std::string value = fvValue(fv);
            
            SWSS_LOG_DEBUG("Transport config field: %s = %s", field.c_str(), value.c_str());
            
            if (field == "default_flow_mode") {
                if (value == "rud") {
                    m_default_flow_mode = UEFlowMode::RELIABLE_UNORDERED_DELIVERY;
                } else if (value == "rod") {
                    m_default_flow_mode = UEFlowMode::RELIABLE_ORDERED_DELIVERY;
                } else if (value == "uud") {
                    m_default_flow_mode = UEFlowMode::UNRELIABLE_UNORDERED_DELIVERY;
                } else if (value == "rudi") {
                    m_default_flow_mode = UEFlowMode::RELIABLE_UNORDERED_DELIVERY_IDEMPOTENT;
                } else {
                    SWSS_LOG_WARN("Unknown flow mode: %s", value.c_str());
                }
            } else if (field == "congestion_algorithm") {
                if (value == "ue_cubic") {
                    m_congestion_algorithm = UECongestionAlgorithm::UE_CUBIC;
                } else if (value == "ue_cubic_plus") {
                    m_congestion_algorithm = UECongestionAlgorithm::UE_CUBIC_PLUS;
                } else if (value == "hybrid") {
                    m_congestion_algorithm = UECongestionAlgorithm::HYBRID;
                } else if (value == "receiver_based") {
                    m_congestion_algorithm = UECongestionAlgorithm::RECEIVER_BASED;
                } else {
                    SWSS_LOG_WARN("Unknown congestion algorithm: %s", value.c_str());
                }
            } else if (field == "default_window_size") {
                try {
                    uint32_t window_size = std::stoi(value);
                    if (window_size >= 1024 && window_size <= 1048576) {  // 1KB to 1MB
                        m_default_window_size = window_size;
                    } else {
                        SWSS_LOG_WARN("Window size out of range: %d", window_size);
                    }
                } catch (const std::exception &e) {
                    SWSS_LOG_ERROR("Failed to parse window_size: %s", e.what());
                }
            } else if (field == "max_flows") {
                try {
                    uint32_t max_flows = std::stoi(value);
                    if (max_flows >= 1000 && max_flows <= 10000000) {  // 1K to 10M flows
                        m_max_flows = max_flows;
                    } else {
                        SWSS_LOG_WARN("Max flows out of range: %d", max_flows);
                    }
                } catch (const std::exception &e) {
                    SWSS_LOG_ERROR("Failed to parse max_flows: %s", e.what());
                }
            } else if (field == "flow_timeout_sec") {
                try {
                    uint32_t timeout = std::stoi(value);
                    if (timeout >= 10 && timeout <= 3600) {  // 10 seconds to 1 hour
                        m_flow_timeout_sec = timeout;
                    } else {
                        SWSS_LOG_WARN("Flow timeout out of range: %d", timeout);
                    }
                } catch (const std::exception &e) {
                    SWSS_LOG_ERROR("Failed to parse flow_timeout_sec: %s", e.what());
                }
            }
        }
        
        SWSS_LOG_NOTICE("Transport configuration updated: mode=%d, algorithm=%d, window=%d", 
                         static_cast<int>(m_default_flow_mode),
                         static_cast<int>(m_congestion_algorithm),
                         m_default_window_size);
        
        // Update application database
        std::vector<FieldValueTuple> fvs;
        fvs.emplace_back("default_flow_mode", std::to_string(static_cast<int>(m_default_flow_mode)));
        fvs.emplace_back("congestion_algorithm", std::to_string(static_cast<int>(m_congestion_algorithm)));
        fvs.emplace_back("default_window_size", std::to_string(m_default_window_size));
        fvs.emplace_back("max_flows", std::to_string(m_max_flows));
        fvs.emplace_back("flow_timeout_sec", std::to_string(m_flow_timeout_sec));
        
        m_appl_db->set(APP_UE_FLOW_TABLE_NAME ":global", fvs);
    }
}

void UEFlowManager::processFlowConfig(const std::string &key, 
                                     const std::string &op,
                                     const std::vector<FieldValueTuple> &values) {
    SWSS_LOG_ENTER();
    
    if (op == SET_COMMAND) {
        for (auto &fv : values) {
            std::string field = fvField(fv);
            std::string value = fvValue(fv);
            
            if (field == "ecn_enable") {
                bool ecn_enabled = (value == "true");
                SWSS_LOG_NOTICE("ECN marking %s", ecn_enabled ? "enabled" : "disabled");
                
                // Update all active flows
                for (auto &flow_pair : m_active_flows) {
                    // In a real implementation, update flow's ECN handling
                    SWSS_LOG_DEBUG("Updated ECN for flow");
                }
            } else if (field == "selective_ack") {
                bool sack_enabled = (value == "true");
                SWSS_LOG_NOTICE("Selective ACK %s", sack_enabled ? "enabled" : "disabled");
            }
        }
    }
}

void UEFlowManager::createFlow(const UEFlowId &flow_id, UEFlowMode mode) {
    SWSS_LOG_ENTER();
    
    if (m_active_flows.size() >= m_max_flows) {
        SWSS_LOG_WARN("Maximum number of flows reached: %d", m_max_flows);
        // In a real implementation, might cleanup oldest flows
        return;
    }
    
    // Check if flow already exists
    if (m_active_flows.find(flow_id) != m_active_flows.end()) {
        SWSS_LOG_WARN("Flow already exists, updating instead of creating");
        return;
    }
    
    UEFlowState state;
    state.flow_id = flow_id;
    state.mode = mode;
    state.sequence_num = 1;
    state.ack_num = 0;
    state.window_size = m_default_window_size;
    state.congestion_window = m_default_window_size;
    state.ssthresh = 65536;
    state.last_activity = time(nullptr);
    state.packet_spraying_enabled = true;
    state.active_paths = 4;  // Default to 4-way ECMP
    state.path_weights = {25, 25, 25, 25};  // Equal weight distribution
    
    m_active_flows[flow_id] = state;
    
    // Initialize statistics
    UEFlowStats stats = {};
    m_flow_stats[flow_id] = stats;
    
    SWSS_LOG_NOTICE("Created UE flow: %s:%d -> %s:%d (mode=%d)",
                     ipToString(flow_id.src_ip).c_str(), flow_id.src_port,
                     ipToString(flow_id.dst_ip).c_str(), flow_id.dst_port,
                     static_cast<int>(mode));
}

void UEFlowManager::removeFlow(const UEFlowId &flow_id) {
    SWSS_LOG_ENTER();
    
    auto flow_it = m_active_flows.find(flow_id);
    if (flow_it != m_active_flows.end()) {
        SWSS_LOG_NOTICE("Removing UE flow: %s:%d -> %s:%d",
                         ipToString(flow_id.src_ip).c_str(), flow_id.src_port,
                         ipToString(flow_id.dst_ip).c_str(), flow_id.dst_port);
        
        m_active_flows.erase(flow_it);
    }
    
    // Remove statistics
    auto stats_it = m_flow_stats.find(flow_id);
    if (stats_it != m_flow_stats.end()) {
        m_flow_stats.erase(stats_it);
    }
    
    // Clean up STATE_DB entry
    std::string stats_key = STATE_UE_FLOW_STATS_TABLE_NAME ":" + 
                           ipToString(flow_id.src_ip) + ":" + 
                           std::to_string(flow_id.src_port) + ":" +
                           ipToString(flow_id.dst_ip) + ":" +
                           std::to_string(flow_id.dst_port);
    m_state_db->del(stats_key);
}

void UEFlowManager::updateFlowState(const UEFlowId &flow_id, const UEFlowState &state) {
    auto flow_it = m_active_flows.find(flow_id);
    if (flow_it != m_active_flows.end()) {
        flow_it->second = state;
        flow_it->second.last_activity = time(nullptr);
    }
}

void UEFlowManager::enablePacketSpraying(const UEFlowId &flow_id, uint8_t num_paths) {
    auto flow_it = m_active_flows.find(flow_id);
    if (flow_it != m_active_flows.end()) {
        flow_it->second.packet_spraying_enabled = true;
        flow_it->second.active_paths = num_paths;
        
        // Initialize equal weights
        flow_it->second.path_weights.clear();
        uint32_t weight_per_path = 100 / num_paths;
        for (uint8_t i = 0; i < num_paths; i++) {
            flow_it->second.path_weights.push_back(weight_per_path);
        }
        
        SWSS_LOG_DEBUG("Enabled packet spraying for flow with %d paths", num_paths);
    }
}

void UEFlowManager::updateFlowPaths(const UEFlowId &flow_id, 
                                   const std::vector<uint32_t> &weights) {
    auto flow_it = m_active_flows.find(flow_id);
    if (flow_it != m_active_flows.end()) {
        flow_it->second.path_weights = weights;
        flow_it->second.active_paths = weights.size();
        
        SWSS_LOG_DEBUG("Updated flow paths: %d paths", (int)weights.size());
    }
}

UEFlowId UEFlowManager::parsePacketFlowId(const uint8_t *packet, size_t len) {
    UEFlowId flow_id = {};
    
    if (len < sizeof(struct iphdr) + sizeof(struct udphdr)) {
        return flow_id;  // Invalid packet
    }
    
    struct iphdr *ip_hdr = (struct iphdr *)packet;
    
    if (ip_hdr->version == 4) {
        flow_id.ip_version = 4;
        flow_id.src_ip = ntohl(ip_hdr->saddr);
        flow_id.dst_ip = ntohl(ip_hdr->daddr);
        
        if (ip_hdr->protocol == IPPROTO_UDP) {
            struct udphdr *udp_hdr = (struct udphdr *)(packet + sizeof(struct iphdr));
            flow_id.src_port = ntohs(udp_hdr->source);
            flow_id.dst_port = ntohs(udp_hdr->dest);
        }
    }
    // TODO: Add IPv6 support
    
    return flow_id;
}

void UEFlowManager::processIncomingPacket(const std::string &interface, 
                                         const uint8_t *packet, size_t len) {
    UEFlowId flow_id = parsePacketFlowId(packet, len);
    
    if (flow_id.src_ip == 0 || flow_id.dst_ip == 0) {
        return;  // Invalid flow ID
    }
    
    // Find or create flow
    auto flow_it = m_active_flows.find(flow_id);
    if (flow_it == m_active_flows.end()) {
        createFlow(flow_id, m_default_flow_mode);
        flow_it = m_active_flows.find(flow_id);
    }
    
    if (flow_it != m_active_flows.end()) {
        // Update statistics
        UEFlowStats &stats = m_flow_stats[flow_id];
        stats.packets_received++;
        stats.bytes_received += len;
        
        // Update flow state
        flow_it->second.last_activity = time(nullptr);
        
        // Process packet based on flow mode
        switch (flow_it->second.mode) {
            case UEFlowMode::RELIABLE_UNORDERED_DELIVERY:
                // Handle RUD packet
                break;
            case UEFlowMode::RELIABLE_ORDERED_DELIVERY:
                // Handle ROD packet - check sequence
                break;
            case UEFlowMode::UNRELIABLE_UNORDERED_DELIVERY:
                // Handle UUD packet - minimal processing
                break;
            case UEFlowMode::RELIABLE_UNORDERED_DELIVERY_IDEMPOTENT:
                // Handle RUDI packet - idempotent semantics
                break;
        }
    }
}

void UEFlowManager::updateFlowRTT(const UEFlowId &flow_id, uint32_t rtt_us) {
    auto stats_it = m_flow_stats.find(flow_id);
    if (stats_it != m_flow_stats.end()) {
        UEFlowStats &stats = stats_it->second;
        
        stats.current_rtt_us = rtt_us;
        
        if (stats.min_rtt_us == 0 || rtt_us < stats.min_rtt_us) {
            stats.min_rtt_us = rtt_us;
        }
        
        if (rtt_us > stats.max_rtt_us) {
            stats.max_rtt_us = rtt_us;
        }
        
        // Update average RTT using exponential weighted moving average
        if (stats.avg_rtt_us == 0) {
            stats.avg_rtt_us = rtt_us;
        } else {
            stats.avg_rtt_us = (stats.avg_rtt_us * 7 + rtt_us) / 8;  // 1/8 weight for new sample
        }
    }
}

void UEFlowManager::doPeriodicTask() {
    // Update flow statistics every 1 second
    static time_t last_stats_update = 0;
    time_t now = time(nullptr);
    
    if (now - last_stats_update >= 1) {
        updateFlowStatistics();
        last_stats_update = now;
    }
    
    // Cleanup expired flows every 60 seconds
    static time_t last_cleanup = 0;
    if (now - last_cleanup >= 60) {
        cleanupExpiredFlows();
        last_cleanup = now;
    }
    
    // Report flow count periodically
    static time_t last_report = 0;
    if (now - last_report >= 300) {  // Every 5 minutes
        SWSS_LOG_NOTICE("Active flows: %zu, Max flows: %d", 
                         m_active_flows.size(), m_max_flows);
        last_report = now;
    }
}

void UEFlowManager::updateFlowStatistics() {
    for (auto &flow_pair : m_flow_stats) {
        const UEFlowId &flow_id = flow_pair.first;
        UEFlowStats &stats = flow_pair.second;
        
        // Update STATE_DB with flow statistics
        std::string stats_key = STATE_UE_FLOW_STATS_TABLE_NAME ":" + 
                               ipToString(flow_id.src_ip) + ":" + 
                               std::to_string(flow_id.src_port) + ":" +
                               ipToString(flow_id.dst_ip) + ":" +
                               std::to_string(flow_id.dst_port);
        
        std::vector<FieldValueTuple> fvs;
        fvs.emplace_back("packets_sent", std::to_string(stats.packets_sent));
        fvs.emplace_back("packets_received", std::to_string(stats.packets_received));
        fvs.emplace_back("bytes_sent", std::to_string(stats.bytes_sent));
        fvs.emplace_back("bytes_received", std::to_string(stats.bytes_received));
        fvs.emplace_back("current_rtt_us", std::to_string(stats.current_rtt_us));
        fvs.emplace_back("min_rtt_us", std::to_string(stats.min_rtt_us));
        fvs.emplace_back("max_rtt_us", std::to_string(stats.max_rtt_us));
        fvs.emplace_back("avg_rtt_us", std::to_string(stats.avg_rtt_us));
        fvs.emplace_back("packets_retransmitted", std::to_string(stats.packets_retransmitted));
        fvs.emplace_back("out_of_order_packets", std::to_string(stats.out_of_order_packets));
        fvs.emplace_back("duplicate_packets", std::to_string(stats.duplicate_packets));
        
        m_state_db->set(stats_key, fvs);
    }
}

void UEFlowManager::cleanupExpiredFlows() {
    SWSS_LOG_ENTER();
    
    time_t now = time(nullptr);
    auto it = m_active_flows.begin();
    size_t flows_removed = 0;
    
    while (it != m_active_flows.end()) {
        const UEFlowId &flow_id = it->first;
        const UEFlowState &state = it->second;
        
        if (now - state.last_activity > m_flow_timeout_sec) {
            SWSS_LOG_DEBUG("Cleaning up expired flow: %s:%d -> %s:%d",
                           ipToString(flow_id.src_ip).c_str(), flow_id.src_port,
                           ipToString(flow_id.dst_ip).c_str(), flow_id.dst_port);
            
            // Remove from statistics
            m_flow_stats.erase(flow_id);
            
            // Clean up STATE_DB entry
            std::string stats_key = STATE_UE_FLOW_STATS_TABLE_NAME ":" + 
                                   ipToString(flow_id.src_ip) + ":" + 
                                   std::to_string(flow_id.src_port) + ":" +
                                   ipToString(flow_id.dst_ip) + ":" +
                                   std::to_string(flow_id.dst_port);
            m_state_db->del(stats_key);
            
            // Remove from active flows
            it = m_active_flows.erase(it);
            flows_removed++;
        } else {
            ++it;
        }
    }
    
    if (flows_removed > 0) {
        SWSS_LOG_NOTICE("Cleaned up %zu expired flows", flows_removed);
    }
}

std::string UEFlowManager::ipToString(uint32_t ip) {
    char buffer[INET_ADDRSTRLEN];
    struct in_addr addr;
    addr.s_addr = htonl(ip);
    inet_ntop(AF_INET, &addr, buffer, INET_ADDRSTRLEN);
    return std::string(buffer);
}
