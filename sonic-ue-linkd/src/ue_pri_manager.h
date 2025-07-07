#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "dbconnector.h"
#include "subscriberstatetable.h"
#include "consumerstatetable.h"
#include "orch.h"

using namespace swss;

#define CFG_UE_PRI_TABLE_NAME "UE_PRI"
#define APP_UE_PRI_GLOBAL_TABLE_NAME "UE_PRI_GLOBAL"
#define STATE_UE_PRI_STATS_TABLE_NAME "UE_PRI_STATS"

struct PRIConfig {
    bool enabled;
    bool ethernet_compression;
    bool ip_compression;
    uint32_t compression_ratio;
    uint32_t min_packet_size;
    uint32_t max_packet_size;
};

struct PRIInterfaceConfig {
    bool enabled;
    bool ethernet_compression;
    bool ip_compression;
    uint32_t compression_ratio;
    bool stats_enable;
    uint32_t compression_threshold;
};

struct PRIStats {
    uint64_t packets_compressed;
    uint64_t packets_uncompressed;
    uint64_t bytes_saved;
    uint64_t compression_ratio_actual;
    uint64_t header_size_reduction;
    uint64_t ethernet_headers_compressed;
    uint64_t ip_headers_compressed;
    uint64_t compression_failures;
    uint64_t bandwidth_improvement_bps;
};

class UEPRIManager : public Orch {
public:
    UEPRIManager(DBConnector *config_db, DBConnector *appl_db, DBConnector *state_db);
    virtual ~UEPRIManager() = default;

    using Orch::doTask;
    void doTask(Consumer &consumer) override;
    void doPeriodicTask();

private:
    void processPRIConfig(const std::string &key, const std::string &op,
                         const std::vector<FieldValueTuple> &values);
    void processInterfaceConfig(const std::string &key, const std::string &op,
                               const std::vector<FieldValueTuple> &values);
    
    void enableGlobalPRI(bool eth_compression, bool ip_compression, uint32_t ratio);
    void disableGlobalPRI();
    
    void enableInterfacePRI(const std::string &interface, const PRIInterfaceConfig &config);
    void disableInterfacePRI(const std::string &interface);
    
    void applyPRIToInterface(const std::string &interface, const PRIInterfaceConfig &config);
    void updatePRIStatistics();
    void updateInterfacePRIStats(const std::string &interface);
    
    bool validatePRIConfig(const PRIConfig &config);
    void calculateCompressionBenefits();
    
    bool getPortOid(const std::string &interface, sai_object_id_t &port_oid);

    DBConnector *m_config_db;
    DBConnector *m_appl_db;
    DBConnector *m_state_db;
    
    ConsumerStateTable m_config_consumer;
    ConsumerStateTable m_interface_consumer;
    
    PRIConfig m_global_pri_config;
    std::unordered_map<std::string, PRIInterfaceConfig> m_pri_interfaces;
    std::unordered_map<std::string, PRIStats> m_pri_stats;
    std::unordered_map<std::string, sai_object_id_t> m_pri_sai_objects;
    
    // Performance tracking
    uint64_t m_total_bytes_saved;
    uint64_t m_total_packets_processed;
    time_t m_last_calculation_time;
};
