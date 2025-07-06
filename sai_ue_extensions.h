#ifndef __SAI_UE_EXTENSIONS_H_
#define __SAI_UE_EXTENSIONS_H_

#include <sai.h>

// Ultra Ethernet object types
typedef enum _sai_object_type_extensions_t {
    SAI_OBJECT_TYPE_UE_LLR = 0x1000,
    SAI_OBJECT_TYPE_UE_PRI = 0x1001,
    SAI_OBJECT_TYPE_UE_TRANSPORT = 0x1002
} sai_object_type_extensions_t;

// Link Layer Retry (LLR) attributes
typedef enum _sai_ue_llr_attr_t {
    SAI_UE_LLR_ATTR_START = 0x00000000,
    SAI_UE_LLR_ATTR_ENABLE,
    SAI_UE_LLR_ATTR_MAX_RETRIES,
    SAI_UE_LLR_ATTR_TIMEOUT_MS,
    SAI_UE_LLR_ATTR_WINDOW_SIZE,
    SAI_UE_LLR_ATTR_SELECTIVE_REPEAT,
    SAI_UE_LLR_ATTR_PORT_ID,
    SAI_UE_LLR_ATTR_STATS_ENABLE,
    SAI_UE_LLR_ATTR_END
} sai_ue_llr_attr_t;

// LLR statistics
typedef enum _sai_ue_llr_stat_t {
    SAI_UE_LLR_STAT_RETRY_COUNT,
    SAI_UE_LLR_STAT_SUCCESS_COUNT,
    SAI_UE_LLR_STAT_TIMEOUT_COUNT,
    SAI_UE_LLR_STAT_LATENCY_IMPROVEMENT_NS
} sai_ue_llr_stat_t;

// Packet Rate Improvement (PRI) attributes  
typedef enum _sai_ue_pri_attr_t {
    SAI_UE_PRI_ATTR_START = 0x00000000,
    SAI_UE_PRI_ATTR_ENABLE,
    SAI_UE_PRI_ATTR_ETHERNET_COMPRESSION,
    SAI_UE_PRI_ATTR_IP_COMPRESSION,
    SAI_UE_PRI_ATTR_COMPRESSION_RATIO,
    SAI_UE_PRI_ATTR_PORT_ID,
    SAI_UE_PRI_ATTR_END
} sai_ue_pri_attr_t;

// LLR API methods
typedef struct _sai_ue_llr_api_t {
    sai_create_ue_llr_fn           create_ue_llr;
    sai_remove_ue_llr_fn           remove_ue_llr;
    sai_set_ue_llr_attribute_fn    set_ue_llr_attribute;
    sai_get_ue_llr_attribute_fn    get_ue_llr_attribute;
    sai_get_ue_llr_stats_fn        get_ue_llr_stats;
} sai_ue_llr_api_t;

// Function prototypes
sai_status_t sai_create_ue_llr(
    _Out_ sai_object_id_t *ue_llr_id,
    _In_ sai_object_id_t switch_id,
    _In_ uint32_t attr_count,
    _In_ const sai_attribute_t *attr_list);

sai_status_t sai_set_ue_llr_attribute(
    _In_ sai_object_id_t ue_llr_id,
    _In_ const sai_attribute_t *attr);

sai_status_t sai_get_ue_llr_stats(
    _In_ sai_object_id_t ue_llr_id,
    _In_ uint32_t number_of_counters,
    _In_ const sai_stat_id_t *counter_ids,
    _Out_ sai_stat_value_t *counters);

#endif /* __SAI_UE_EXTENSIONS_H_ */
