#include "ue_congestion_manager.h"
#include "logger.h"
#include "tokenize.h"
#include <algorithm>

UECongestionManager::UECongestionManager(DBConnector *config_db, 
                                        DBConnector *appl_db,
                                        DBConnector *state_db) :
    m_config_db(config_db),
    m_appl_db(appl_db),
    m_state_db(state_db),
    m_config_consumer(config_db, CFG_UE_CONGESTION_TABLE_NAME),
    m_interface_consumer(config_db, CFG_UE_INTERFACE_TABLE_NAME),
    m_algorithm(UECongestionAlgorithm::UE_CUBIC_PLUS),
    m_ecn_threshold_percent(80),
    m_drop_threshold_percent(95),
    m_real_time_feedback(true),
    m_path_rebalancing_enabled(true),
    m_adaptive_spraying_enabled(true),
    m_congestion_detection_interval_ms(100),
    m_last_congestion_check(0),
    m_last_path_rebalance(0),
    m_last_stats_update(0)
{
    SWSS_LOG_ENTER();
    SWSS_LOG_NOTICE("Ultra Ethernet Congestion Manager initialized");
}

void UECongestionManager::doTask(Consumer &consumer) {
    SWSS_LOG_ENTER();
    
    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end()) {
        KeyOpFieldsValuesTuple t = it->second;
        
        std::string key = kfvKey(t);
        std::string op = kfvOp(t);
        auto values = kfvFieldsValues(t);
        
        if (&consumer == &m_config_consumer) {
            processCongestionConfig(key, op, values);
        } else if (&consumer == &m_interface_consumer) {
            processInterfaceConfig(key, op, values);
        }
        
        it = consumer.m_toSync.erase(it);
    }
}

void UECongestionManager::processCongestionConfig(const std::string &key, 
                                                 const std::string &op,
                                                 const std::vector<FieldValueTuple> &values) {
    SWSS_LOG_ENTER();
    
    if (key != "global") {
        SWSS_LOG_WARN("Unknown congestion config key: %s", key.c_str());
        return;
    }
    
    if (op == SET_COMMAND) {
        for (auto &fv : values) {
            std::string field = fvField(fv);
            std::string value = fvValue(fv);
            
            if (field == "algorithm") {
                if (value == "ue_cubic") {
                    m_algorithm = UECongestionAlgorithm::UE_CUBIC;
                } else if (value == "ue_cubic_plus") {
                    m_algorithm = UECongestionAlgorithm::UE_CUBIC_PLUS;
                } else if (value == "hybrid") {
                    m_algorithm = UECongestionAlgorithm::HYBRID;
                } else if (value == "receiver_based") {
                    m_algorithm = UECongestionAlgorithm::RECEIVER_BASED;
                }
            } else if (field == "ecn_threshold_percent") {
                m_ecn_threshold_percent = std::stoi(value);
            } else if (field == "drop_threshold_percent") {
                m_drop_threshold_percent = std::stoi(value);
            } else if (field == "real_time_feedback") {
                m_real_time_feedback = (value == "true");
            } else if (field == "path_rebalancing") {
                m_path_rebalancing_enabled = (value == "true");
            } else if (field == "adaptive_spraying") {
                m_adaptive_spraying_enabled = (value == "true");
            }
        }
        
        SWSS_LOG_NOTICE("Congestion control updated: algorithm=%d, ecn_threshold=%d%%",
                         static_cast<int>(m_algorithm), m_ecn_threshold_percent);
    }
}

void UECongestionManager::doPeriodicTask() {
    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    // Congestion detection
    if (now_ms - m_last_congestion_check >= m_congestion_detection_interval_ms) {
        detectCongestion();
        updateCongestionState();
        m_last_congestion_check = now_ms;
    }
    
    // Path rebalancing
    if (m_path_rebalancing_enabled && 
        now_ms - m_last_path_rebalance >= 1000) {  // Every 1 second
        rebalancePaths();
        m_last_path_rebalance = now_ms;
    }
    
    // Statistics update
    if (now_ms - m_last_stats_update >= 5000) {  // Every 5 seconds
        updateCongestionStatistics();
        m_last_stats_update = now_ms;
    }
}

void UECongestionManager::detectCongestion() {
    // In a real implementation, this would query queue depths from SAI
    // For now, we'll simulate congestion detection
    
    for (auto &interface_pair : m_interface_congestion) {
        std::string interface = interface_pair.first;
        CongestionInfo &info = interface_pair.second;
        
        // Simulate queue depth reading (in real implementation, query from SAI)
        uint32_t current_queue_depth = rand() % 100;  // 0-100% utilization
        
        info.queue_depth = current_queue_depth;
        info.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        CongestionState old_state = info.state;
        
        // Determine congestion state based on thresholds
        if (current_queue_depth >= info.threshold_critical) {
            info.state = CongestionState::CRITICAL;
        } else if (current_queue_depth >= info.threshold_congested) {
            info.state = CongestionState::CONGESTED;
        } else if (current_queue_depth >= info.threshold_warning) {
            info.state = CongestionState::WARNING;
        } else {
            info.state = CongestionState::NORMAL;
        }
        
        // Handle state changes
        if (info.state != old_state) {
            handleCongestionEvent(interface, info.state);
        }
    }
}

void UECongestionManager::handleCongestionEvent(const std::string &interface, 
                                               CongestionState state) {
    SWSS_LOG_NOTICE("Congestion state change on %s: %d", 
                     interface.c_str(), static_cast<int>(state));
    
    // Update statistics
    CongestionStats &stats = m_congestion_stats[interface];
    stats.congestion_events++;
    
    switch (state) {
        case CongestionState::WARNING:
            // Enable ECN marking
            enableECNMarking(interface, m_ecn_threshold_percent);
            break;
            
        case CongestionState::CONGESTED:
            // Start aggressive congestion control
            stats.ecn_marked_packets += 100;  // Simulate ECN marking
            
            // Trigger path rebalancing
            if (m_path_rebalancing_enabled) {
                rebalancePaths();
                stats.path_rebalance_events++;
            }
            break;
            
        case CongestionState::CRITICAL:
            // Emergency measures - may need to drop packets
            stats.dropped_packets += 10;  // Simulate packet drops
            break;
            
        case CongestionState::NORMAL:
            // Congestion cleared
            break;
    }
    
    // Add to event queue for reporting
    CongestionInfo event_info = m_interface_congestion[interface];
    m_congestion_events.push(event_info);
    
    // Keep only last 100 events
    while (m_congestion_events.size() > 100) {
        m_congestion_events.pop();
    }
}

void UECongestionManager::rebalancePaths() {
    SWSS_LOG_ENTER();
    
    // Update path weights based on congestion state
    updatePathWeights();
    
    // Apply new weights to active flows
    // In a real implementation, this would interact with flow manager
    // to update packet spraying weights
    
    SWSS_LOG_DEBUG("Path rebalancing completed");
}

void UECongestionManager::updatePathWeights() {
    for (auto &path_pair : m_path_info) {
        std::string interface = path_pair.first;
        PathInfo &path = path_pair.second;
        
        // Adjust weight based on congestion state
        switch (path.congestion_state) {
            case CongestionState::NORMAL:
                path.weight = 100;  // Full weight
                path.available = true;
                break;
                
            case CongestionState::WARNING:
                path.weight = 75;   // Reduced weight
                path.available = true;
                break;
                
            case CongestionState::CONGESTED:
                path.weight = 25;   // Minimal weight
                path.available = true;
                break;
                
            case CongestionState::CRITICAL:
                path.weight = 0;    // No traffic
                path.available = false;
                break;
        }
    }
}

void UECongestionManager::enableECNMarking(const std::string &interface, 
                                          uint32_t threshold) {
    SWSS_LOG_NOTICE("Enabling ECN marking on %s at %d%% threshold", 
                     interface.c_str(), threshold);
    
    // In a real implementation, this would configure SAI to enable ECN marking
    // when queue depth exceeds the threshold
    
    // Update application database
    std::vector<FieldValueTuple> fvs;
    fvs.emplace_back("ecn_enable", "true");
    fvs.emplace_back("ecn_threshold", std::to_string(threshold));
    
    std::string config_key = APP_UE_CONGESTION_STATE_TABLE_NAME ":" + interface;
    m_appl_db->set(config_key, fvs);
}

void UECongestionManager::updateCongestionStatistics() {
    for (auto &stats_pair : m_congestion_stats) {
        const std::string &interface = stats_pair.first;
        const CongestionStats &stats = stats_pair.second;
        
        // Update STATE_DB with congestion statistics
        std::string stats_key = STATE_UE_CONGESTION_STATS_TABLE_NAME ":" + interface;
        
        std::vector<FieldValueTuple> fvs;
        fvs.emplace_back("congestion_events", std::to_string(stats.congestion_events));
        fvs.emplace_back("ecn_marked_packets", std::to_string(stats.ecn_marked_packets));
        fvs.emplace_back("dropped_packets", std::to_string(stats.dropped_packets));
        fvs.emplace_back("path_rebalance_events", std::to_string(stats.path_rebalance_events));
        fvs.emplace_back("avg_queue_depth", std::to_string(stats.avg_queue_depth));
        fvs.emplace_back("max_queue_depth", std::to_string(stats.max_queue_depth));
        
        m_state_db->set(stats_key, fvs);
    }
}
