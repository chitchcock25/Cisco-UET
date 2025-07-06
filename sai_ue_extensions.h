// File: sai_ue_extensions.h
#include <sai.h>

// UET-specific SAI attributes
typedef enum _sai_ue_attr_t {
    SAI_UE_ATTR_START = 0x10000000,
    SAI_UE_ATTR_PACKET_SPRAY_ENABLE,
    SAI_UE_ATTR_CONGESTION_CONTROL_MODE,
    SAI_UE_ATTR_ENTROPY_HASH_SEED,
    SAI_UE_ATTR_FLOW_LOAD_BALANCE_MODE,
    SAI_UE_ATTR_SELECTIVE_RETRANSMIT_ENABLE,
    SAI_UE_ATTR_END
} sai_ue_attr_t;

// Flow control API extensions
sai_status_t sai_create_ue_flow_entry(
    _Out_ sai_object_id_t *ue_flow_id,
    _In_ sai_object_id_t switch_id,
    _In_ uint32_t attr_count,
    _In_ const sai_attribute_t *attr_list);

sai_status_t sai_set_ue_congestion_control(
    _In_ sai_object_id_t switch_id,
    _In_ const sai_attribute_t *attr);
