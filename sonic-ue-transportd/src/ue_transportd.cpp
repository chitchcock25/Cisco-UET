#include <iostream>
#include <memory>
#include <signal.h>
#include "dbconnector.h"
#include "select.h"
#include "logger.h"
#include "ue_flow_manager.h"
#include "ue_congestion_manager.h"

using namespace swss;

bool g_running = true;

void signal_handler(int sig) {
    SWSS_LOG_NOTICE("Received signal %d, exiting", sig);
    g_running = false;
}

int main(int argc, char **argv) {
    Logger::getInstance().setMinPrio(Logger::SWSS_INFO);
    
    SWSS_LOG_ENTER();
    SWSS_LOG_NOTICE("Starting Ultra Ethernet Transport Daemon");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        DBConnector config_db("CONFIG_DB", 0);
        DBConnector appl_db("APPL_DB", 0);
        DBConnector state_db("STATE_DB", 0);
        
        UEFlowManager flow_manager(&config_db, &appl_db, &state_db);
        UECongestionManager congestion_manager(&config_db, &appl_db, &state_db);
        
        Select s;
        s.addSelectable(&flow_manager);
        s.addSelectable(&congestion_manager);
        
        while (g_running) {
            Selectable *sel;
            int ret = s.select(&sel, 1000);
            
            if (ret == Select::OBJECT) {
                sel->readData();
            } else if (ret == Select::TIMEOUT) {
                flow_manager.doPeriodicTask();
                congestion_manager.doPeriodicTask();
            }
        }
        
    } catch (const std::exception &e) {
        SWSS_LOG_ERROR("Exception: %s", e.what());
        return 1;
    }
    
    return 0;
}
