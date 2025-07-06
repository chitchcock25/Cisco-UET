#!/usr/bin/env python3
# File: /usr/local/bin/ue_dual_stack_config_mgr.py

import json
import ipaddress
import socket
import syslog
from swsscommon import swsscommon

class UEDualStackConfigManager:
    def __init__(self):
        # Connect to SONIC databases
        self.config_db = swsscommon.ConfigDBConnector()
        self.config_db.connect()
        
        self.appl_db = swsscommon.DBConnector("APPL_DB", 0)
        self.state_db = swsscommon.DBConnector("STATE_DB", 0)
        
        # Track supported IP versions per interface
        self.interface_capabilities = {}
        
        # Subscribe to configuration changes
        self.subscriber = swsscommon.SubscriberStateTable(
            self.config_db.get_redis_client(), "UE_GLOBAL"
        )
        
    def load_ue_dual_stack_config(self):
        """Load Ultra Ethernet dual-stack configuration"""
        try:
            # Load global UE settings
            global_config = self.config_db.get_table("UE_GLOBAL")
            if global_config and "global" in global_config:
                self.apply_global_dual_stack_config(global_config["global"])
            
            # Load address family specific settings
            af_config = self.config_db.get_table("UE_ADDRESS_FAMILY")
            if af_config:
                self.apply_address_family_config(af_config)
            
            # Load per-interface settings
            interface_config = self.config_db.get_table("UE_INTERFACE") 
            for interface, config in interface_config.items():
                self.apply_interface_dual_stack_config(interface, config)
                
            # Load ECMP configuration
            ecmp_config = self.config_db.get_table("UE_ECMP_CONFIG")
            if ecmp_config:
                self.apply_ecmp_dual_stack_config(ecmp_config)
                
            # Load RDMA configuration
            rdma_config = self.config_db.get_table("UE_RDMA_CONFIG")
            if rdma_config:
                self.apply_rdma_dual_stack_config(rdma_config)
                
        except Exception as e:
            syslog.syslog(syslog.LOG_ERR, f"UE dual-stack config load failed: {e}")
            
    def apply_global_dual_stack_config(self, config):
        """Apply global dual-stack UE configuration"""
        if config.get("enable") == "true":
            # Parse supported IP versions
            ip_versions = config.get("ip_versions", "4,6").split(",")
            ipv4_enabled = "4" in ip_versions
            ipv6_enabled = "6" in ip_versions
            
            # Store global dual-stack state
            self.appl_db.hset("UE_GLOBAL_STATE", "enabled", "true")
            self.appl_db.hset("UE_GLOBAL_STATE", "ipv4_enabled", str(ipv4_enabled).lower())
            self.appl_db.hset("UE_GLOBAL_STATE", "ipv6_enabled", str(ipv6_enabled).lower())
            
            # Configure transport mode
            mode = config.get("transport_mode", "uet")
            self.appl_db.hset("UE_TRANSPORT_MODE", "mode", mode)
            
            # Set dual-stack mode
            dual_stack_mode = config.get("dual_stack_mode", "concurrent")
            self.appl_db.hset("UE_DUAL_STACK", "mode", dual_stack_mode)
            
            # Set congestion control algorithm  
            cc_mode = config.get("congestion_control", "hybrid")
            self.appl_db.hset("UE_CONGESTION_CONTROL", "algorithm", cc_mode)
            
            syslog.syslog(syslog.LOG_INFO, 
                         f"Ultra Ethernet enabled globally - IPv4: {ipv4_enabled}, IPv6: {ipv6_enabled}")
        else:
            self.appl_db.hset("UE_GLOBAL_STATE", "enabled", "false")
            
    def apply_address_family_config(self, config):
        """Apply address family specific configuration"""
        for af, af_config in config.items():
            if af == "ipv4" and af_config.get("enabled") == "true":
                # Configure IPv4 specific settings
                udp_port = af_config.get("udp_port", "4791")
                entropy_fields = af_config.get("entropy_fields", "").split(",")
                
                self.appl_db.hset("UE_IPV4_CONFIG", "enabled", "true")
                self.appl_db.hset("UE_IPV4_CONFIG", "udp_port", udp_port)
                self.appl_db.hset("UE_IPV4_CONFIG", "entropy_fields", 
                                json.dumps(entropy_fields))
                
            elif af == "ipv6" and af_config.get("enabled") == "true":
                # Configure IPv6 specific settings
                udp_port = af_config.get("udp_port", "4791")
                entropy_fields = af_config.get("entropy_fields", "").split(",")
                
                self.appl_db.hset("UE_IPV6_CONFIG", "enabled", "true")
                self.appl_db.hset("UE_IPV6_CONFIG", "udp_port", udp_port)
                self.appl_db.hset("UE_IPV6_CONFIG", "entropy_fields", 
                                json.dumps(entropy_fields))
            
    def apply_interface_dual_stack_config(self, interface, config):
        """Apply per-interface dual-stack UE configuration"""
        if config.get("ue_enable") == "true":
            # Parse supported IP versions for this interface
            ip_versions = config.get("ip_versions", "4,6").split(",")
            ipv4_enabled = "4" in ip_versions
            ipv6_enabled = "6" in ip_versions
            
            # Store interface capabilities
            self.interface_capabilities[interface] = {
                "ipv4": ipv4_enabled,
                "ipv6": ipv6_enabled
            }
            
            # Store interface-specific config in APPL_DB
            interface_key = f"UE_INTERFACE_CONFIG:{interface}"
            
            self.appl_db.hset(interface_key, "enabled", "true")
            self.appl_db.hset(interface_key, "ipv4_enabled", str(ipv4_enabled).lower())
            self.appl_db.hset(interface_key, "ipv6_enabled", str(ipv6_enabled).lower())
            
            # Configure IPv4 specific settings
            if ipv4_enabled:
                max_paths_v4 = config.get("max_paths_v4", "4")
                self.appl_db.hset(interface_key, "max_paths_v4", max_paths_v4)
                
            # Configure IPv6 specific settings
            if ipv6_enabled:
                max_paths_v6 = config.get("max_paths_v6", "4")
                self.appl_db.hset(interface_key, "max_paths_v6", max_paths_v6)
                
            # Common settings
            lb_mode = config.get("load_balance_mode", "ecmp")
            self.appl_db.hset(interface_key, "load_balance_mode", lb_mode)
            
            # IP version preference
            prefer_version = config.get("prefer_version", "4")
            self.appl_db.hset(interface_key, "prefer_version", prefer_version)
            
            # Trigger orchagent to apply to hardware
            self._notify_orchagent_dual_stack(interface, config)
            
    def apply_ecmp_dual_stack_config(self, config):
        """Apply ECMP configuration for both IPv4 and IPv6"""
        # Configure IPv4 ECMP groups
        if "ipv4_groups" in config:
            for group_name, group_config in config["ipv4_groups"].items():
                self._configure_ecmp_group("ipv4", group_name, group_config)
                
        # Configure IPv6 ECMP groups  
        if "ipv6_groups" in config:
            for group_name, group_config in config["ipv6_groups"].items():
                self._configure_ecmp_group("ipv6", group_name, group_config)
                
    def _configure_ecmp_group(self, ip_version, group_name, group_config):
        """Configure ECMP group for specific IP version"""
        group_key = f"UE_ECMP_GROUP:{ip_version}:{group_name}"
        
        prefix = group_config.get("prefix", "")
        max_paths = group_config.get("max_paths", "4")
        hash_algo = group_config.get("hash_algorithm", "crc32")
        
        # Validate prefix format
        try:
            if ip_version == "ipv4":
                ipaddress.IPv4Network(prefix)
            else:
                ipaddress.IPv6Network(prefix)
        except ValueError as e:
            syslog.syslog(syslog.LOG_ERR, f"Invalid {ip_version} prefix {prefix}: {e}")
            return
            
        self.appl_db.hset(group_key, "prefix", prefix)
        self.appl_db.hset(group_key, "max_paths", max_paths)
        self.appl_db.hset(group_key, "hash_algorithm", hash_algo)
        self.appl_db.hset(group_key, "ip_version", ip_version)
        
    def apply_rdma_dual_stack_config(self, config):
        """Apply RDMA configuration for dual-stack"""
        if "global" in config:
            rdma_config = config["global"]
            
            # Connection limits per IP version
            max_conn_v4 = rdma_config.get("max_connections_v4", "500000")
            max_conn_v6 = rdma_config.get("max_connections_v6", "500000")
            
            self.appl_db.hset("UE_RDMA_CONFIG", "max_connections_v4", max_conn_v4)
            self.appl_db.hset("UE_RDMA_CONFIG", "max_connections_v6", max_conn_v6)
            
            # Connection mode
            conn_mode = rdma_config.get("connection_mode", "ephemeral")
            self.appl_db.hset("UE_RDMA_CONFIG", "connection_mode", conn_mode)
            
            # Timeout settings
            timeout = rdma_config.get("connection_timeout", "30")
            self.appl_db.hset("UE_RDMA_CONFIG", "connection_timeout", timeout)
            
    def _notify_orchagent_dual_stack(self, interface, config):
        """Notify orchagent of dual-stack configuration changes"""
        notification = {
            "interface": interface,
            "operation": "SET_DUAL_STACK",
            "config": config,
            "capabilities": self.interface_capabilities.get(interface, {})
        }
        
        self.appl_db.publish("UE_DUAL_STACK_CONFIG_CHANNEL", json.dumps(notification))
        
    def validate_dual_stack_config(self, config):
        """Validate dual-stack configuration"""
        errors = []
        
        # Check global IP version settings
        if "UE_GLOBAL" in config:
            global_config = config["UE_GLOBAL"].get("global", {})
            ip_versions = global_config.get("ip_versions", "4,6").split(",")
            
            valid_versions = ["4", "6"]
            for version in ip_versions:
                if version.strip() not in valid_versions:
                    errors.append(f"Invalid IP version: {version}")
                    
        # Check interface compatibility
        if "UE_INTERFACE" in config:
            for interface, iface_config in config["UE_INTERFACE"].items():
                iface_versions = iface_config.get("ip_versions", "4,6").split(",")
                
                # Check if max_paths are specified for enabled versions
                if "4" in iface_versions and "max_paths_v4" not in iface_config:
                    errors.append(f"Interface {interface}: max_paths_v4 required when IPv4 enabled")
                    
                if "6" in iface_versions and "max_paths_v6" not in iface_config:
                    errors.append(f"Interface {interface}: max_paths_v6 required when IPv6 enabled")
                    
        # Validate ECMP prefixes
        if "UE_ECMP_CONFIG" in config:
            ecmp_config = config["UE_ECMP_CONFIG"]
            
            if "ipv4_groups" in ecmp_config:
                for group_name, group_config in ecmp_config["ipv4_groups"].items():
                    try:
                        ipaddress.IPv4Network(group_config.get("prefix", ""))
                    except ValueError:
                        errors.append(f"Invalid IPv4 prefix in group {group_name}")
                        
            if "ipv6_groups" in ecmp_config:
                for group_name, group_config in ecmp_config["ipv6_groups"].items():
                    try:
                        ipaddress.IPv6Network(group_config.get("prefix", ""))
                    except ValueError:
                        errors.append(f"Invalid IPv6 prefix in group {group_name}")
                        
        return errors
        
    def get_interface_statistics(self, interface):
        """Get dual-stack statistics for interface"""
        stats = {}
        
        # IPv4 statistics
        ipv4_stats_key = f"UE_INTERFACE_STATS_V4:{interface}"
        stats["ipv4"] = {
            "packets_sprayed": self.state_db.hget(ipv4_stats_key, "packets_sprayed") or "0",
            "active_paths": self.state_db.hget(ipv4_stats_key, "active_paths") or "0",
            "connections": self.state_db.hget(ipv4_stats_key, "active_connections") or "0"
        }
        
        # IPv6 statistics
        ipv6_stats_key = f"UE_INTERFACE_STATS_V6:{interface}"
        stats["ipv6"] = {
            "packets_sprayed": self.state_db.hget(ipv6_stats_key, "packets_sprayed") or "0",
            "active_paths": self.state_db.hget(ipv6_stats_key, "active_paths") or "0", 
            "connections": self.state_db.hget(ipv6_stats_key, "active_connections") or "0"
        }
        
        return stats
        
    def auto_detect_ip_capabilities(self, interface):
        """Auto-detect IP version capabilities of interface"""
        capabilities = {"ipv4": False, "ipv6": False}
        
        try:
            # Check if interface has IPv4 address
            ipv4_key = f"INTERFACE|{interface}"
            ipv4_config = self.config_db.get_entry("INTERFACE", interface)
            if ipv4_config:
                capabilities["ipv4"] = True
                
            # Check if interface has IPv6 address
            ipv6_key = f"INTERFACE|{interface}"
            # In SONIC, IPv6 addresses are stored with different keys
            ipv6_entries = self.config_db.get_keys("INTERFACE")
            for key in ipv6_entries:
                if key.startswith(f"{interface}|") and ":" in key:
                    capabilities["ipv6"] = True
                    break
                    
        except Exception as e:
            syslog.syslog(syslog.LOG_WARNING, f"Failed to detect IP capabilities for {interface}: {e}")
            
        return capabilities

# CLI integration for dual-stack commands
def cli_ue_dual_stack_global(enable, ipv4, ipv6, mode, congestion_control):
    """Configure global dual-stack Ultra Ethernet settings"""
    config_db = swsscommon.ConfigDBConnector()
    config_db.connect()
    
    ip_versions = []
    if ipv4:
        ip_versions.append("4")
    if ipv6:
        ip_versions.append("6")
        
    if not ip_versions:
        ip_versions = ["4", "6"]  # Default to both
        
    ue_global_config = {
        "enable": "true" if enable else "false",
        "transport_mode": mode,
        "congestion_control": congestion_control,
        "packet_spraying": "true" if enable else "false",
        "ip_versions": ",".join(ip_versions),
        "dual_stack_mode": "concurrent"
    }
    
    config_db.set_entry("UE_GLOBAL", "global", ue_global_config)
    print(f"Ultra Ethernet dual-stack global config updated: {ue_global_config}")

def cli_ue_dual_stack_interface(interface, enable, ipv4, ipv6, max_paths_v4, max_paths_v6, prefer_version):
    """Configure dual-stack Ultra Ethernet on interface"""
    config_db = swsscommon.ConfigDBConnector()
    config_db.connect()
    
    ip_versions = []
    if ipv4:
        ip_versions.append("4")
    if ipv6:
        ip_versions.append("6")
        
    if not ip_versions:
        ip_versions = ["4", "6"]  # Default to both
        
    interface_config = {
        "ue_enable": "true" if enable else "false",
        "ip_versions": ",".join(ip_versions),
        "load_balance_mode": "entropy_spray",
        "prefer_version": str(prefer_version)
    }
    
    if ipv4:
        interface_config["max_paths_v4"] = str(max_paths_v4)
    if ipv6:
        interface_config["max_paths_v6"] = str(max_paths_v6)
        
    config_db.set_entry("UE_INTERFACE", interface, interface_config)
    print(f"Ultra Ethernet dual-stack config for {interface}: {interface_config}")

# Example usage:
if __name__ == "__main__":
    manager = UEDualStackConfigManager()
    
    # Load configuration
    manager.load_ue_dual_stack_config()
    
    # Get interface statistics
    stats = manager.get_interface_statistics("Ethernet0")
    print(f"Interface stats: {json.dumps(stats, indent=2)}")
    
    # Auto-detect capabilities
    caps = manager.auto_detect_ip_capabilities("Ethernet0")
    print(f"Interface capabilities: {caps}")
