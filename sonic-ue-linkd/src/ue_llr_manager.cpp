#include "ue_llr_manager.h"
#include "logger.h"
#include "tokenize.h"
#include "sai.h"
#include <chrono>
#include <algorithm>

UELLRManager::UELLRManager(DBConnector *config_db, 
                          DBConnector *appl_db,
                          DBConnector *state_db) :
    m_config_db(config_db),
    m_appl_db(appl_db),
    m_state_db(state_db),
    m_config_consumer(config_db, CFG_UE_LINK_LAYER_TABLE_NAME),
    m_interface_consumer(config_db, CFG_UE_INTERFACE_TABLE_NAME)
{
    SWSS_LOG_ENTER();
    
    // Initialize global LLR configuration with defaults
    m_global_llr_config.enabled = false;
    m_global_llr_config.max_retries = 3;
    m_global_llr_config.timeout_ms = 5;
    m_global_llr_config.window_size = 256;
    m_global_llr_config.selective_repeat = true;
    
    SWSS_LOG_NOTICE("Ultra Ethernet LLR Manager initialized");
}

void UELLRManager::doTask(Consumer &consumer) {
    SWSS_LOG_ENTER();
    
    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end()) {
        KeyOpFieldsValuesTuple t = it->second;
        
        std::string key = kfvKey(t);
        std::string op = kfvOp(t);
        auto values = kfvFieldsValues(t);
        
        SWSS_LOG_DEBUG("Processing LLR task: key=%s, op=%s", key.c_str(), op.c_str());
        
        try {
            if (&consumer == &m_config_consumer) {
                processLLRConfig(key, op, values);
            } else if (&consumer == &m_interface_consumer) {
                processInterfaceConfig(key, op, values);
            }
        } catch (const std::exception &e) {
            SWSS_LOG_ERROR("Exception processing LLR task: %s", e.what());
        }
        
        it = consumer.m_toSync.erase(it);
    }
}

void UELLRManager::processLLRConfig(const std::string &key, const std::string &op,
                                   const std::vector<FieldValueTuple> &values) {
    SWSS_LOG_ENTER();
    
    if (key != "global") {
        SWSS_LOG_WARN("Unknown LLR config key: %s", key.c_str());
        return;
    }
    
    if (op == SET_COMMAND) {
        bool llr_enabled = false;
        uint32_t max_retries = 3;
        uint32_t timeout_ms = 5;
        uint32_t window_size = 256;
        bool selective_repeat = true;
        
        // Parse configuration values
        for (auto &fv : values) {
            std::string field = fvField(fv);
            std::string value = fvValue(fv);
            
            SWSS_LOG_DEBUG("LLR config field: %s = %s", field.c_str(), value.c_str());
            
            if (field == "llr_enable") {
                llr_enabled = (value == "true");
            } else if (field == "llr_max_retries") {
                try {
                    max_retries = std::stoi(value);
                    if (max_retries < 1 || max_retries > 10) {
                        SWSS_LOG_ERROR("Invalid max_retries value: %d", max_retries);
                        max_retries = 3;  // Use default
                    }
                } catch (const std::exception &e) {
                    SWSS_LOG_ERROR("Failed to parse max_retries: %s", e.what());
                }
            } else if (field == "llr_timeout_ms") {
                try {
                    timeout_ms = std::stoi(value);
                    if (timeout_ms < 1 || timeout_ms > 100) {
                        SWSS_LOG_ERROR("Invalid timeout_ms value: %d", timeout_ms);
                        timeout_ms = 5;  // Use default
                    }
                } catch (const std::exception &e) {
                    SWSS_LOG_ERROR("Failed to parse timeout_ms: %s", e.what());
                }
            } else if (field == "llr_window_size") {
                try {
                    window_size = std::stoi(value);
                    // Validate power of 2
                    if (window_size == 0 || (window_size & (window_size - 1)) != 0) {
                        SWSS_LOG_ERROR("Window size must be power of 2: %d", window_size);
                        window_size = 256;  // Use default
                    }
                    if (window_size < 16 || window_size > 1024) {
                        SWSS_LOG_ERROR("Window size out of range: %d", window_size);
                        window_size = 256;  // Use default
                    }
                } catch (const std::exception &e) {
                    SWSS_LOG_ERROR("Failed to parse window_size: %s", e.what());
                }
            } else if (field == "llr_selective_repeat") {
                selective_repeat = (value == "true");
            }
        }
        
        // Apply global LLR configuration
        if (llr_enabled) {
            enableGlobalLLR(max_retries, timeout_ms, window_size, selective_repeat);
        } else {
            disableGlobalLLR();
        }
        
    } else if (op == DEL_COMMAND) {
        disableGlobalLLR();
    }
}

void UELLRManager::processInterfaceConfig(const std::string &key, 
                                         const std::string &op,
                                         const std::vector<FieldValueTuple> &values) {
    SWSS_LOG_ENTER();
    
    std::string interface = key;
    
    if (op == SET_COMMAND) {
        LLRInterfaceConfig config;
        config.enabled = false;
        config.max_retries = m_global_llr_config.max_retries;
        config.timeout_ms = m_global_llr_config.timeout_ms;
        config.buffer_size = 1024;
        config.stats_enable = true;
        
        bool found_llr_config = false;
        
        for (auto &fv : values) {
            std::string field = fvField(fv);
            std::string value = fvValue(fv);
            
            if (field == "llr_enable") {
                config.enabled = (value == "true");
                found_llr_config = true;
            } else if (field == "ue_enable" && value == "true") {
                // Interface is enabled for Ultra Ethernet
                found_llr_config = true;
            }
            // Note: In a real implementation, you'd parse more complex nested config
            // like llr_per_interface settings from JSON or similar
        }
        
        if (found_llr_config) {
            if (config.enabled) {
                enableInterfaceLLR(interface, config);
            } else {
                disableInterfaceLLR(interface);
            }
        }
        
    } else if (op == DEL_COMMAND) {
        disableInterfaceLLR(interface);
    }
}

void UELLRManager::enableGlobalLLR(uint32_t max_retries, 
                                  uint32_t timeout_ms,
                                  uint32_t window_size,
                                  bool selective_repeat) {
    SWSS_LOG_NOTICE("Enabling global LLR: retries=%d, timeout=%dms, window=%d, selective=%s",
                     max_retries, timeout_ms, window_size, 
                     selective_repeat ? "true" : "false");
    
    // Store global configuration
    m_global_llr_config.enabled = true;
    m_global_llr_config.max_retries = max_retries;
    m_global_llr_config.timeout_ms = timeout_ms;
    m_global_llr_config.window_size = window_size;
    m_global_llr_config.selective_repeat = selective_repeat;
    
    // Update application database
    std::vector<FieldValueTuple> fvs;
    fvs.emplace_back("enabled", "true");
    fvs.emplace_back("max_retries", std::to_string(max_retries));
    fvs.emplace_back("timeout_ms", std::to_string(timeout_ms));
    fvs.emplace_back("window_size", std::to_string(window_size));
    fvs.emplace_back("selective_repeat", selective_repeat ? "true" : "false");
    
    m_appl_db->set(APP_UE_LLR_GLOBAL_TABLE_NAME ":global", fvs);
    
    // Apply to all enabled interfaces
    for (auto &interface : m_llr_interfaces) {
        if (interface.second.enabled) {
            applyLLRToInterface(interface.first, interface.second);
        }
    }
}

void UELLRManager::disableGlobalLLR() {
    SWSS_LOG_NOTICE("Disabling global LLR");
    
    m_global_llr_config.enabled = false;
    
    // Update application database
    std::vector<FieldValueTuple> fvs;
    fvs.emplace_back("enabled", "false");
    
    m_appl_db->set(APP_UE_LLR_GLOBAL_TABLE_NAME ":global", fvs);
    
    // Disable on all interfaces
    for (auto &interface : m_llr_interfaces) {
        disableInterfaceLLR(interface.first);
    }
    
    // Clear SAI objects
    m_llr_sai_objects.clear();
}

void UELLRManager::enableInterfaceLLR(const std::string &interface, 
                                     const LLRInterfaceConfig &config) {
    SWSS_LOG_NOTICE("Enabling LLR on interface %s: retries=%d, timeout=%dms", 
                     interface.c_str(), config.max_retries, config.timeout_ms);
    
    m_llr_interfaces[interface] = config;
    
    // Apply configuration if global LLR is enabled
    if (m_global_llr_config.enabled) {
        applyLLRToInterface(interface, config);
    }
    
    // Initialize statistics
    LLRStats stats = {};
    m_llr_stats[interface] = stats;
}

void UELLRManager::disableInterfaceLLR(const std::string &interface) {
    SWSS_LOG_NOTICE("Disabling LLR on interface %s", interface.c_str());
    
    // Remove from configuration
    m_llr_interfaces.erase(interface);
    
    // Clean up SAI objects
    auto sai_it = m_llr_sai_objects.find(interface);
    if (sai_it != m_llr_sai_objects.end()) {
        // In a real implementation, call SAI to remove LLR object
        // sai_remove_ue_llr(sai_it->second);
        m_llr_sai_objects.erase(sai_it);
    }
    
    // Remove statistics
    m_llr_stats.erase(interface);
    
    // Clean up application database
    std::string config_key = APP_UE_LLR_GLOBAL_TABLE_NAME ":" + interface;
    m_appl_db->del(config_key);
    
    std::string stats_key = STATE_UE_LLR_STATS_TABLE_NAME ":" + interface;
    m_state_db->del(stats_key);
}

void UELLRManager::applyLLRToInterface(const std::string &interface, 
                                      const LLRInterfaceConfig &config) {
    SWSS_LOG_ENTER();
    
    // Get SAI port object ID
    sai_object_id_t port_oid;
    if (!getPortOid(interface, port_oid)) {
        SWSS_LOG_ERROR("Failed to get port OID for interface %s", interface.c_str());
        return;
    }
    
    // In a real implementation, this would create SAI LLR object:
    /*
    std::vector<sai_attribute_t> attrs;
    
    sai_attribute_t attr;
    attr.id = SAI_UE_LLR_ATTR_PORT_ID;
    attr.value.oid = port_oid;
    attrs.push_back(attr);
    
    attr.id = SAI_UE_LLR_ATTR_ENABLE;
    attr.value.booldata = true;
    attrs.push_back(attr);
    
    attr.id = SAI_UE_LLR_ATTR_MAX_RETRIES;
    attr.value.u32 = config.max_retries;
    attrs.push_back(attr);
    
    sai_object_id_t llr_oid;
    sai_status_t status = sai_create_ue_llr(&llr_oid, gSwitchId, attrs.size(), attrs.data());
    
    if (status == SAI_STATUS_SUCCESS) {
        m_llr_sai_objects[interface] = llr_oid;
    }
    */
    
    // For now, just update the application database
    std::vector<FieldValueTuple> fvs;
    fvs.emplace_back("enabled", "true");
    fvs.emplace_back("max_retries", std::to_string(config.max_retries));
    fvs.emplace_back("timeout_ms", std::to_string(config.timeout_ms));
    fvs.emplace_back("buffer_size", std::to_string(config.buffer_size));
    fvs.emplace_back("port_oid", std::to_string(port_oid));
    
    std::string config_key = APP_UE_LLR_GLOBAL_TABLE_NAME ":" + interface;
    m_appl_db->set(config_key, fvs);
    
    SWSS_LOG_NOTICE("LLR applied to interface %s", interface.c_str());
}

void UELLRManager::doPeriodicTask() {
    // Update LLR statistics every 5 seconds
    static time_t last_stats_update = 0;
    time_t now = time(nullptr);
    
    if (now - last_stats_update >= 5) {
        updateLLRStatistics();
        last_stats_update = now;
    }
}

void UELLRManager::updateLLRStatistics() {
    for (auto &interface : m_llr_interfaces) {
        if (interface.second.enabled && interface.second.stats_enable) {
            updateInterfaceLLRStats(interface.first);
        }
    }
}

void UELLRManager::updateInterfaceLLRStats(const std::string &interface) {
    // In a real implementation, this would query LLR statistics from SAI:
    /*
    auto sai_it = m_llr_sai_objects.find(interface);
    if (sai_it != m_llr_sai_objects.end()) {
        std::vector<sai_stat_id_t> counter_ids = {
            SAI_UE_LLR_STAT_RETRY_COUNT,
            SAI_UE_LLR_STAT_SUCCESS_COUNT,
            SAI_UE_LLR_STAT_TIMEOUT_COUNT
        };
        
        std::vector<sai_stat_value_t> counters(counter_ids.size());
        sai_status_t status = sai_get_ue_llr_stats(sai_it->second, 
                                                   counter_ids.size(), 
                                                   counter_ids.data(), 
                                                   counters.data());
        
        if (status == SAI_STATUS_SUCCESS) {
            LLRStats &stats = m_llr_stats[interface];
            stats.retry_count = counters[0].u64;
            stats.success_count = counters[1].u64;
            stats.timeout_count = counters[2].u64;
        }
    }
    */
    
    // For now, simulate statistics
    LLRStats &stats = m_llr_stats[interface];
    
    // Simulate realistic LLR statistics
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> retry_dist(0, 10);
    static std::uniform_int_distribution<> success_dist(90, 100);
    
    uint64_t new_retries = retry_dist(gen);
    uint64_t new_successes = success_dist(gen);
    
    stats.retry_count += new_retries;
    stats.success_count += new_successes;
    stats.frames_transmitted += new_retries + new_successes;
    stats.frames_retransmitted += new_retries;
    
    // Calculate derived statistics
    if (stats.frames_transmitted > 0) {
        double retry_rate = (double)stats.retry_count / stats.frames_transmitted;
        // Estimate latency improvement (more retries = more improvement from LLR)
        stats.latency_improvement_ns = (uint64_t)(retry_rate * 1000000);  // microseconds to nanoseconds
    }
    
    // Update STATE_DB with current statistics
    std::string stats_key = STATE_UE_LLR_STATS_TABLE_NAME ":" + interface;
    
    std::vector<FieldValueTuple> fvs;
    fvs.emplace_back("retry_count", std::to_string(stats.retry_count));
    fvs.emplace_back("success_count", std::to_string(stats.success_count));
    fvs.emplace_back("timeout_count", std::to_string(stats.timeout_count));
    fvs.emplace_back("latency_improvement_ns", std::to_string(stats.latency_improvement_ns));
    fvs.emplace_back("frames_transmitted", std::to_string(stats.frames_transmitted));
    fvs.emplace_back("frames_retransmitted", std::to_string(stats.frames_retransmitted));
    
    // Calculate success rate
    if (stats.frames_transmitted > 0) {
        double success_rate = (double)stats.success_count / stats.frames_transmitted * 100.0;
        fvs.emplace_back("success_rate_percent", std::to_string((uint32_t)success_rate));
    }
    
    m_state_db->set(stats_key, fvs);
    
    SWSS_LOG_DEBUG("Updated LLR stats for %s: retries=%llu, successes=%llu", 
                   interface.c_str(), stats.retry_count, stats.success_count);
}

bool UELLRManager::getPortOid(const std::string &interface, sai_object_id_t &port_oid) {
    // In a real implementation, this would query the port table:
    /*
    auto port_table = m_appl_db->hgetall("PORT_TABLE:" + interface);
    if (port_table.find("oid") != port_table.end()) {
        port_oid = std::stoull(port_table["oid"], nullptr, 16);
        return true;
    }
    */
    
    // For simulation, generate a consistent fake OID based on interface name
    port_oid = 0x1000000000000000ULL | (std::hash<std::string>{}(interface) & 0xFFFFFFFFFFFFULL);
    return true;
}
