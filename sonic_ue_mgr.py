# File: sonic_ue_mgr.py
import sonic_py_common.logger as log
from swsscommon import swsscommon

class UltraEthernetManager:
    def __init__(self):
        self.logger = log.getLogger(__name__)
        self.config_db = swsscommon.ConfigDBConnector()
        self.state_db = swsscommon.DBConnector("STATE_DB", 0)
        
    def configure_packet_spraying(self, interface, enable=True):
        """Configure packet spraying for UET"""
        try:
            # Update SAI configuration
            sai_attrs = {
                "SAI_UE_ATTR_PACKET_SPRAY_ENABLE": str(enable).lower(),
                "SAI_UE_ATTR_FLOW_LOAD_BALANCE_MODE": "ecmp_spray"
            }
            
            # Apply to interface
            self._apply_sai_config(interface, sai_attrs)
            self.logger.info(f"Packet spraying {('enabled' if enable else 'disabled')} on {interface}")
            
        except Exception as e:
            self.logger.error(f"Failed to configure packet spraying: {e}")
            
    def setup_congestion_control(self, mode="hybrid"):
        """Setup UET congestion control"""
        congestion_config = {
            "mode": mode,  # "sender", "receiver", "hybrid"
            "algorithm": "ue_cubic",
            "ecn_enable": True,
            "selective_ack": True
        }
        
        # Apply system-wide congestion control
        self.config_db.set_entry("UE_CONGESTION", "GLOBAL", congestion_config)
