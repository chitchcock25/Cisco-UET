#!/bin/bash

. /usr/local/bin/asic_status.sh

function wait_for_database_service() {
    # Wait for redis to start
    until [[ $(redis-cli ping | grep -c PONG) -gt 0 ]]; do
        sleep 1
    done
    
    # Wait for configDB to be populated
    until [[ $(redis-cli -n 4 KEYS | wc -l) -gt 0 ]]; do
        sleep 1
    done
}

function start_ue_linkd() {
    echo "Starting Ultra Ethernet Link Layer Daemon..."
    
    # Wait for database
    wait_for_database_service
    
    # Start the daemon
    exec /usr/bin/ue-linkd
}

case "$1" in
    start|"")
        start_ue_linkd
        ;;
    stop)
        echo "Stopping Ultra Ethernet Link Layer Daemon..."
        pkill -f ue-linkd
        ;;
    *)
        echo "Usage: $0 {start|stop}"
        exit 1
        ;;
esac
