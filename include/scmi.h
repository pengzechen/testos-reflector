
/*

scmi {
    compatible = "arm,scmi-smc";
    shmem = <0x40>;
    arm,smc-id = <0x82000010>;
    #address-cells = <0x01>;
    #size-cells = <0x00>;
    phandle = <0x1fe>;

    protocol@14 {
        reg = <0x14>;
        #clock-cells = <0x01>;
        assigned-clocks = <0x0e 0x00 0x0e 0x02 0x0e 0x03>;
        assigned-clock-rates = <0x30a32c00 0x30a32c00 0x30a32c00>;
        phandle = <0x0e>;
    };

    protocol@16 {
        reg = <0x16>;
        #reset-cells = <0x01>;
        phandle = <0x107>;
    };
};

sram@10f000 {
    compatible = "mmio-sram";
    reg = <0x00 0x10f000 0x00 0x100>;
    #address-cells = <0x01>;
    #size-cells = <0x01>;
    ranges = <0x00 0x00 0x10f000 0x100>;

    sram@0 {
        compatible = "arm,scmi-shmem";
        reg = <0x00 0x100>;
        phandle = <0x40>;
    };
};

参考: DEN0056F_System_Control_and_Management_Interface_v4.0-alp0.pdf  page 88

*/

#ifndef __SCMI_H__
#define __SCMI_H__


#include "t_types.h"


#define SCMI_SMC_CALL   0x82000010
#define SCMI_SHMEM_BASE 0x10F000
#define SCMI_SHMEM_SIZE 0x100

enum scmi_std_protocol
{
    SCMI_PROTOCOL_BASE     = 0x10,
    SCMI_PROTOCOL_POWER    = 0x11,
    SCMI_PROTOCOL_SYSTEM   = 0x12,
    SCMI_PROTOCOL_PERF     = 0x13,
    SCMI_PROTOCOL_CLOCK    = 0x14,
    SCMI_PROTOCOL_SENSOR   = 0x15,
    SCMI_PROTOCOL_RESET    = 0x16,
    SCMI_PROTOCOL_VOLTAGE  = 0x17,
    SCMI_PROTOCOL_POWERCAP = 0x18,
};

enum scmi_clock_protocol_cmd
{
    CLOCK_ATTRIBUTES                   = 0x3,
    CLOCK_DESCRIBE_RATES               = 0x4,
    CLOCK_RATE_SET                     = 0x5,
    CLOCK_RATE_GET                     = 0x6,
    CLOCK_CONFIG_SET                   = 0x7,
    CLOCK_NAME_GET                     = 0x8,
    CLOCK_RATE_NOTIFY                  = 0x9,
    CLOCK_RATE_CHANGE_REQUESTED_NOTIFY = 0xA,
};

// ref: DEN0056F_System_Control_and_Management_Interface_v4.0-alp0.pdf, 3.1.2 Message format, Table3:Messageheaderformat
// SCMI 消息头结构体
struct scmi_msg_header
{
    uint32_t message_id : 8;    // [7:0]   消息 ID
    uint32_t message_type : 2;  // [9:8]   消息类型 (0=Command, 2=Response)
    uint32_t protocol_id : 8;   // [17:10] 协议 ID
    uint32_t token : 10;        // [27:18] Token，用于请求响应匹配
    uint32_t reserved : 4;      // [31:28] 必须为0
} __attribute__((packed));

static inline uint32_t
scmi_pack_header(uint8_t message_id, uint8_t message_type, uint8_t protocol_id, uint16_t token)
{
    return ((message_id & 0xFF) | ((message_type & 0x3) << 8) | ((protocol_id & 0xFF) << 10) |
            ((token & 0x3FF) << 18));
}

static inline void
scmi_unpack_header(uint32_t  header,
                   uint8_t  *message_id,
                   uint8_t  *message_type,
                   uint8_t  *protocol_id,
                   uint16_t *token)
{
    if (message_id)
        *message_id = header & 0xFF;  // bits [7:0]
    if (message_type)
        *message_type = (header >> 8) & 0x3;  // bits [9:8]
    if (protocol_id)
        *protocol_id = (header >> 10) & 0xFF;  // bits [17:10]
    if (token)
        *token = (header >> 18) & 0x3FF;  // bits [27:18]
}

// SCMI 消息结构体
struct scmi_msg
{
    void  *buf;
    size_t len;
};

struct scmi_shared_mem
{
    uint32_t reserved;
    uint32_t channel_status;
#define SCMI_SHMEM_CHAN_STAT_CHANNEL_ERROR BIT(1)
#define SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE  BIT(0)
    uint32_t reserved1[2];
    uint32_t flags;
#define SCMI_SHMEM_FLAG_INTR_ENABLED BIT(0)
    uint32_t length;
    uint32_t msg_header;
};


#define PAYLOAD_OFFSET (sizeof(struct scmi_shared_mem) + 4)


// 3.6.2.1 PROTOCOL_VERSION <page 88>
struct scmi_msg_resp_protocl_version
{
    uint32_t version;
};


#define SCMI_SHORT_NAME_MAX_SIZE 64
struct get_clock_name_pld
{
    uint32_t flags;
    uint8_t  name[SCMI_SHORT_NAME_MAX_SIZE];
};


void
scmi_attr_get(uint32_t clock_id);

void
enable_scmi_clock(uint32_t clock_id);

#endif  // __SCMI_H__