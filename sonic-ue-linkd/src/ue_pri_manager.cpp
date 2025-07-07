#include "ue_pri_manager.h"
#include "logger.h"
#include "tokenize.h"

UEPRIManager::UEPRIManager(DBConnector *config_db, 
                          DBConnector *appl_db,
                          DBConnector *state_db) :
    m_config_db(config_db),
    m_appl_db(appl_db),
    m_state_db(state_db),
    m_config_consumer(config_db, CFG_UE_PRI_TABLE_NAME),
    m_interface_consumer(config_db, CFG_UE_INTERFACE_TABLE_NAME)
{
    SWSS_LOG_ENTER();
    SWSS_LOG_NOTICE("Ultra Ethernet PRI Manager initialized");
}

void UEPRIManager::doTask(Consumer &consumer) {
    SWSS_LOG_ENTER();
    
    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end()) {
        KeyOpFieldsValuesTuple t = it->second;
        
        std::string key = kfvKey(t);
        std::string op = kfvOp(t);
        auto values = kfvFieldsValues(t);
        
        if (&consumer == &m_config_consumer) {
            processPRIConfig(key, op, values);
        } else if (&consumer == &m_interface_consumer) {
            processInterfaceConfig(key, op, values);
        }
        
        it = consumer.m_toSync.erase(it);
    }
}

void UEPRIManager::processPRIConfig(const std::string &key, 
                                   const std::string &op,
                                   const std::vector<FieldValueTuple> &values) {
    SWSS_LOG_ENTER();
    
    if (key != "global") {
        SWSS_LOG_WARN("Unknown PRI config key: %s", key.c_str());
        return;
    }
    
    if (op == SET_COMMAND) {
        bool pri_enabled = false;
        bool eth_compression = false;
        bool ip_compression = false;
        uint32_t compression_ratio = 25;
        
        for (auto &fv : values) {
            std::string field = fvField(fv);
            std::string value = fvValue(fv);
            
            if (field == "pri_enable") {
                pri_enabled = (value == "true");
            } else if (field == "ethernet_compression") {
                eth_compression = (value == "true");
            } else if (field == "ip_compression") {
                ip_compression = (value == "true");
            } else if (field == "compression_ratio") {
                compression_ratio = std::stoi(value);
            }
        }
        
        if (pri_enabled) {
            enableGlobalPRI(eth_compression, ip_compression, compression_ratio);
        } else {
            disableGlobalPRI();
        }
    }
}

void UEPRIManager::enableGlobalPRI(bool eth_compression, 
                                  bool ip_compression, 
                                  uint32_t ratio) {
    SWSS_LOG_NOTICE("Enabling global PRI: eth=%s, ip=%s, ratio=%d%%",
                     eth_compression ? "true" : "false",
                     ip_compression ? "true" : "false", 
                     ratio);
    
    m_global_pri_config.enabled = true;
    m_global_pri_config.ethernet_compression = eth_compression;
    m_global_pri_config.ip_compression = ip_compression;
    m_global_pri_config.compression_ratio = ratio;
    
    // Update application database
    std::vector<FieldValueTuple> fvs;
    fvs.emplace_back("enabled", "true");
    fvs.emplace_back("ethernet_compression", eth_compression ? "true" : "false");
    fvs.emplace_back("ip_compression", ip_compression ? "true" : "false");
    fvs.emplace_back("compression_ratio", std::to_string(ratio));
    
    m_appl_db->set(APP_UE_PRI_GLOBAL_TABLE_NAME ":global", fvs);
}

void UEPRIManager::doPeriodicTask() {
    // Update PRI statistics every 5 seconds
    static time_t last_stats_update = 0;
    time_t now = time(nullptr);
    
    if (now - last_stats_update >= 5) {
        updatePRIStatistics();
        last_stats_update = now;
    }
}

void UEPRIManager::updatePRIStatistics() {
    for (auto &interface : m_pri_interfaces) {
        if (interface.second.enabled) {
            updateInterfacePRIStats(interface.first);
        }
    }
}

void UEPRIManager::updateInterfacePRIStats(const std::string &interface) {
    // In a real implementation, this would query hardware statistics
    // For now, we'll simulate some data
    
    PRIStats &stats = m_pri_stats[interface];
    
    // Simulate statistics (in real implementation, get from SAI/hardware)
    stats.packets_compressed += 1000;
    stats.packets_uncompressed += 50;
    stats.bytes_saved += stats.packets_compressed * 
                        (m_global_pri_config.compression_ratio * 42 / 100);  // Assume 42-byte headers
    stats.compression_ratio_actual = (stats.bytes_saved * 100) / 
                                   (stats.packets_compressed * 42);
    
    // Update STATE_DB
    std::string stats_key = STATE_UE_PRI_STATS_TABLE_NAME ":" + interface;
    
    std::vector<FieldValueTuple> fvs;
    fvs.emplace_back("packets_compressed", std::to_string(stats.packets_compressed));
    fvs.emplace_back("packets_uncompressed", std::to_string(stats.packets_uncompressed));
    fvs.emplace_back("bytes_saved", std::to_string(stats.bytes_saved));
    fvs.emplace_back("compression_ratio_actual", std::to_string(stats.compression_ratio_actual));
    
    m_state_db->set(stats_key, fvs);
}
