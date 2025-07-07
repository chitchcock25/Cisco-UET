#include <iostream>
#include <memory>
#include <signal.h>
#include <unistd.h>
#include "dbconnector.h"
#include "select.h"
#include "logger.h"
#include "ue_llr_manager.h"
#include "ue_pri_manager.h"

using namespace swss;

bool g_running = true;

void signal_handler(int sig) {
    SWSS_LOG_NOTICE("Received signal %d, exiting", sig);
    g_running = false;
}

int main(int argc, char **argv) {
    Logger::getInstance().setMinPrio(Logger::SWSS_INFO);
    
    SWSS_LOG_ENTER();
    SWSS_LOG_NOTICE("Starting Ultra Ethernet Link Layer Daemon");
    
    // Signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // Database connections
        DBConnector config_db("CONFIG_DB", 0);
        DBConnector appl_db("APPL_DB", 0);
        DBConnector state_db("STATE_DB", 0);
        
        // Create managers
        UELLRManager llr_manager(&config_db, &appl_db, &state_db);
        UEPRIManager pri_manager(&config_db, &appl_db, &state_db);
        
        // Select for event processing
        Select s;
        s.addSelectable(&llr_manager);
        s.addSelectable(&pri_manager);
        
        // Main event loop
        while (g_running) {
            Selectable *sel;
            int ret = s.select(&sel, 1000); // 1 second timeout
            
            if (ret == Select::OBJECT) {
                sel->readData();
            } else if (ret == Select::TIMEOUT) {
                // Periodic processing
                llr_manager.doPeriodicTask();
                pri_manager.doPeriodicTask();
            }
        }
        
    } catch (const std::exception &e) {
        SWSS_LOG_ERROR("Exception: %s", e.what());
        return 1;
    }
    
    SWSS_LOG_NOTICE("Ultra Ethernet Link Layer Daemon stopped");
    return 0;
}
