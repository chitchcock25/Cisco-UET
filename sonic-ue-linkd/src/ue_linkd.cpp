/**
 * Ultra Ethernet Link Daemon for SONiC
 * Manages Link-Level Retry (LLR), Packet Rate Improvement (PRI),
 * and FEC monitoring for Ultra Ethernet
 */

#include <iostream>
#include <memory>
#include <thread>
#include <signal.h>
#include <unistd.h>
#include "swss/dbconnector.h"
#include "swss/select.h"
#include "swss/table.h"
#include "swss/subscriberstatetable.h"
#include "swss/notificationproducer.h"
#include "swss/logger.h"

using namespace std;
using namespace swss;

class UELinkD {
private:
    shared_ptr<DBConnector> m_appDb;
    shared_ptr<DBConnector> m_configDb;
    shared_ptr<DBConnector> m_stateDb;
    shared_ptr<DBConnector> m_countersDb;
    
    unique_ptr<Table> m_ueLinkTable;
    unique_ptr<Table> m_ueLinkStateTable;
    unique_ptr<Table> m_fecStatsTable;
    unique_ptr<ProducerStateTable> m_appUeLinkTable;
    unique_ptr<SubscriberStateTable> m_cfgUeLinkTable;
    
    bool m_running;

public:
    UELinkD() : m_running(true) {
        SWSS_LOG_ENTER();
        
        // Initialize database connections
        m_appDb = make_shared<DBConnector>("APPL_DB", 0);
        m_configDb = make_shared<DBConnector>("CONFIG_DB", 0);
        m_stateDb = make_shared<DBConnector>("STATE_DB", 0);
        m_countersDb = make_shared<DBConnector>("COUNTERS_DB", 0);
        
        // Initialize tables
        m_ueLinkTable = make_unique<Table>(m_appDb.get(), "UE_LINK_TABLE");
        m_ueLinkStateTable = make_unique<Table>(m_stateDb.get(), "UE_LINK_STATE_TABLE");
        m_fecStatsTable = make_unique<Table>(m_countersDb.get(), "UE_FEC_STATS_TABLE");
        m_appUeLinkTable = make_unique<ProducerStateTable>(m_appDb.get(), "UE_LINK_TABLE");
        m_cfgUeLinkTable = make_unique<SubscriberStateTable>(m_configDb.get(), "UE_LINK_TABLE");
        
        SWSS_LOG_NOTICE("UE Link Daemon initialized");
    }
    
    void processLinkConfig(const string& key, const vector<FieldValueTuple>& data) {
        SWSS_LOG_ENTER();
        
        string port = key;
        bool llr_enabled = false;
        bool pri_enabled = false;
        string fec_mode = "none";
        
        for (auto& fv : data) {
            string field = fvField(fv);
            string value = fvValue(fv);
            
            if (field == "llr_enable") {
                llr_enabled = (value == "true");
            } else if (field == "pri_enable") {
                pri_enabled = (value == "true");
            } else if (field == "fec_mode") {
                fec_mode = value;
            }
        }
        
        SWSS_LOG_NOTICE("Configuring port %s: LLR=%d, PRI=%d, FEC=%s",
                       port.c_str(), llr_enabled, pri_enabled, fec_mode.c_str());
        
        // Update application DB with processed config
        vector<FieldValueTuple> fvVector;
        fvVector.push_back(FieldValueTuple("llr_enabled", llr_enabled ? "true" : "false"));
        fvVector.push_back(FieldValueTuple("pri_enabled", pri_enabled ? "true" : "false"));
        fvVector.push_back(FieldValueTuple("fec_mode", fec_mode));
        fvVector.push_back(FieldValueTuple("admin_status", "up"));
        
        m_appUeLinkTable->set(port, fvVector);
        
        // Initialize LLR if enabled
        if (llr_enabled) {
            initializeLLR(port);
        }
        
        // Initialize PRI if enabled
        if (pri_enabled) {
            initializePRI(port);
        }
        
        // Setup FEC monitoring
        if (fec_mode != "none") {
            setupFECMonitoring(port, fec_mode);
        }
    }
    
    void initializeLLR(const string& port) {
        SWSS_LOG_ENTER();
        
        // LLR parameters
        vector<FieldValueTuple> llrParams;
        llrParams.push_back(FieldValueTuple("retry_count", "3"));
        llrParams.push_back(FieldValueTuple("retry_timeout_ms", "100"));
        llrParams.push_back(FieldValueTuple("window_size", "128"));
        llrParams.push_back(FieldValueTuple("state", "active"));
        
        string llrKey = "LLR|" + port;
        m_ueLinkStateTable->set(llrKey, llrParams);
        
        SWSS_LOG_NOTICE("LLR initialized for port %s", port.c_str());
    }
    
    void initializePRI(const string& port) {
        SWSS_LOG_ENTER();
        
        // PRI compression parameters
        vector<FieldValueTuple> priParams;
        priParams.push_back(FieldValueTuple("compression_mode", "aggressive"));
        priParams.push_back(FieldValueTuple("header_optimization", "enabled"));
        priParams.push_back(FieldValueTuple("state", "negotiating"));
        
        string priKey = "PRI|" + port;
        m_ueLinkStateTable->set(priKey, priParams);
        
        // Trigger LLDP negotiation
        triggerLLDPNegotiation(port);
        
        SWSS_LOG_NOTICE("PRI initialized for port %s", port.c_str());
    }
    
    void setupFECMonitoring(const string& port, const string& fec_mode) {
        SWSS_LOG_ENTER();
        
        // Initialize FEC counters
        vector<FieldValueTuple> fecCounters;
        fecCounters.push_back(FieldValueTuple("corrected_codewords", "0"));
        fecCounters.push_back(FieldValueTuple("uncorrected_codewords", "0"));
        fecCounters.push_back(FieldValueTuple("total_codewords", "0"));
        fecCounters.push_back(FieldValueTuple("pre_fec_ber", "0.0"));
        fecCounters.push_back(FieldValueTuple("post_fec_ber", "0.0"));
        
        m_fecStatsTable->set(port, fecCounters);
        
        // Start FEC monitoring thread
        thread fecMonitor(&UELinkD::monitorFEC, this, port);
        fecMonitor.detach();
        
        SWSS_LOG_NOTICE("FEC monitoring started for port %s with mode %s",
                       port.c_str(), fec_mode.c_str());
    }
    
    void triggerLLDPNegotiation(const string& port) {
        SWSS_LOG_ENTER();
        
        // Send LLDP TLV for UE capability negotiation
        vector<FieldValueTuple> lldpTlv;
        lldpTlv.push_back(FieldValueTuple("ue_capable", "true"));
        lldpTlv.push_back(FieldValueTuple("llr_supported", "true"));
        lldpTlv.push_back(FieldValueTuple("pri_supported", "true"));
        lldpTlv.push_back(FieldValueTuple("inc_supported", "true"));
        lldpTlv.push_back(FieldValueTuple("uet_version", "1.0"));
        
        // This would interface with lldpmgr
        Table lldpTable(m_appDb.get(), "LLDP_CUSTOM_TLV_TABLE");
        string lldpKey = port + "|UE_CAPABILITIES";
        lldpTable.set(lldpKey, lldpTlv);
        
        SWSS_LOG_INFO("LLDP UE capability negotiation triggered for port %s", port.c_str());
    }
    
    void monitorFEC(const string& port) {
        SWSS_LOG_ENTER();
        
        while (m_running) {
            // In real implementation, this would read from hardware
            // For now, simulate FEC statistics
            
            vector<string> values;
            m_fecStatsTable->get(port, values);
            
            // Simulate incrementing counters
            uint64_t total_codewords = stoull(values[2]) + 1000;
            uint64_t corrected = stoull(values[0]) + rand() % 10;
            uint64_t uncorrected = stoull(values[1]) + (rand() % 100 == 0 ? 1 : 0);
            
            double pre_fec_ber = (double)(corrected + uncorrected) / total_codewords;
            double post_fec_ber = (double)uncorrected / total_codewords;
            
            vector<FieldValueTuple> fecUpdate;
            fecUpdate.push_back(FieldValueTuple("corrected_codewords", to_string(corrected)));
            fecUpdate.push_back(FieldValueTuple("uncorrected_codewords", to_string(uncorrected)));
            fecUpdate.push_back(FieldValueTuple("total_codewords", to_string(total_codewords)));
            fecUpdate.push_back(FieldValueTuple("pre_fec_ber", to_string(pre_fec_ber)));
            fecUpdate.push_back(FieldValueTuple("post_fec_ber", to_string(post_fec_ber)));
            
            m_fecStatsTable->set(port, fecUpdate);
            
            // Check thresholds and raise alerts if needed
            if (post_fec_ber > 1e-12) {
                SWSS_LOG_WARN("High post-FEC BER detected on port %s: %e",
                             port.c_str(), post_fec_ber);
            }
            
            sleep(10); // Update every 10 seconds
        }
    }
    
    void run() {
        SWSS_LOG_ENTER();
        
        Select s;
        s.addSelectable(m_cfgUeLinkTable.get());
        
        while (m_running) {
            Selectable *sel;
            int ret = s.select(&sel, 1000); // 1 second timeout
            
            if (ret == Select::ERROR) {
                SWSS_LOG_ERROR("Select error");
                continue;
            }
            
            if (ret == Select::TIMEOUT) {
                continue;
            }
            
            auto *cfgTable = dynamic_cast<SubscriberStateTable *>(sel);
            if (cfgTable == m_cfgUeLinkTable.get()) {
                KeyOpFieldsValuesTuple kfv;
                cfgTable->pop(kfv);
                
                string key = kfvKey(kfv);
                string op = kfvOp(kfv);
                
                if (op == SET_COMMAND) {
                    processLinkConfig(key, kfvFieldsValues(kfv));
                } else if (op == DEL_COMMAND) {
                    SWSS_LOG_NOTICE("Removing UE config for %s", key.c_str());
                    m_appUeLinkTable->del(key);
                }
            }
        }
    }
    
    void stop() {
        m_running = false;
    }
};

static UELinkD *g_ueLinkD = nullptr;

void sigterm_handler(int signo) {
    SWSS_LOG_NOTICE("Received signal %d, shutting down", signo);
    if (g_ueLinkD) {
        g_ueLinkD->stop();
    }
}

int main(int argc, char **argv) {
    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_INFO);
    
    SWSS_LOG_NOTICE("Starting Ultra Ethernet Link Daemon");
    
    // Register signal handlers
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);
    
    try {
        g_ueLinkD = new UELinkD();
        g_ueLinkD->run();
        delete g_ueLinkD;
    } catch (const exception& e) {
        SWSS_LOG_ERROR("Exception: %s", e.what());
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
