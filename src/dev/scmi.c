

#include "scmi.h"
#include "lib/t_logger.h"
#include "t_types.h"
#include "lib/t_string.h"
#include "t_sysreg.h"

bool
do_smc()
{
    uint64_t ret0, ret1, ret2, ret3;

    asm volatile("mov x0, %4\n"
                 "smc    #0\n"
                 : "=r"(ret0), "=r"(ret1), "=r"(ret2), "=r"(ret3)  // 输出约束
                 : "r"(SCMI_SMC_CALL)                              // 输入参数
                 : "x0", "x1", "x2", "x3", "memory"                // clobber list
    );

    if (ret0 != 0) {
        logger_error("SCMI: SMC call failed with return code: %lu\n", ret0);
        return false;
    }

    return true;
}

void
protocto_version_check()
{
    logger("protocol version check\n");
    // 1、准备消息头
    uint32_t hd = scmi_pack_header(0,                    // message_id
                                   0,                    // message_type
                                   SCMI_PROTOCOL_CLOCK,  // protocol_id
                                   0                     // token
    );


    // 2、准备消息体
    // get version 没有额外的 payload
    int32_t payload_size = 0;

    // 3、写入共享内存
    struct scmi_shared_mem *shmem = (struct scmi_shared_mem *) SCMI_SHMEM_BASE;
    void * shmem_addr = (void *) SCMI_SHMEM_BASE;
    shmem->channel_status         = 0;
    shmem->flags                  = 0;
    shmem->length                 = sizeof(hd) + payload_size;
    shmem->msg_header             = hd;

    DSB_SY();

    if (!do_smc()) {
        logger_error("SCMI: SMC call failed.\n");
        return;
    }

    // 4、读取响应头、响应长度、相应状态
    uint8_t  msg_id, msg_type, proto_id;
    uint16_t token;
    int32_t status;
    scmi_unpack_header(shmem->msg_header, &msg_id, &msg_type, &proto_id, &token);
    logger_info("SCMI Response Header: message_id=%d, message_type=%d\n", msg_id, msg_type);
    logger_info("SCMI Response Header: protocol_id=%d, token=%d\n", proto_id, token);

    logger_info("SCMI Response Length: %u\n", shmem->length);
    memcpy((void *) &status, (void *) (shmem_addr + sizeof(struct scmi_shared_mem)), sizeof(status));
    logger_info("SCMI Response Status: %d\n", status);


    // 5、读取响应 payload
    struct scmi_msg_resp_protocl_version resp_payload;
    logger_info("sizeof(resp_payload) = %u\n", sizeof(resp_payload));

    // 拷贝响应 payload
    memcpy((void *) &resp_payload,
           (void *) (shmem_addr + PAYLOAD_OFFSET),
           sizeof(resp_payload));

    logger_info("SCMI Base Protocol Version Response: version=0x%x\n", resp_payload.version);
}


//  =============================== 4.6.2.9 CLOCK_NAME_GET 
void
clock_name_get(uint32_t id)
{
    logger("clock name get\n");
    // 1、准备消息头
    uint32_t hd = scmi_pack_header(CLOCK_NAME_GET,     // message_id
                                   0,                    // message_type
                                   SCMI_PROTOCOL_CLOCK,  // protocol_id
                                   7                     // token
    );

    // 2、准备payload
    uint32_t clock_id     = id;  // 查询时钟ID
    int32_t  payload_size = sizeof(clock_id);


    // 3、写入共享内存
    struct scmi_shared_mem *shmem = (struct scmi_shared_mem *) SCMI_SHMEM_BASE;
    void * shmem_addr = (void *) SCMI_SHMEM_BASE;
    shmem->channel_status         = 0;
    shmem->flags                  = 0;
    shmem->length                 = sizeof(hd) + payload_size;
    shmem->msg_header             = hd;

    DSB_SY();

    // 写入 payload
    memcpy((void *) (shmem_addr + sizeof(struct scmi_shared_mem)), (void *) &clock_id, sizeof(clock_id));


    // 3、发起SMC调用
    if (!do_smc()) {
        logger_error("SCMI: SMC call failed.\n");
        return;
    }


    // 4、读取响应头、响应长度、相应状态
    uint8_t  msg_id, msg_type, proto_id;
    uint16_t token;
    uint32_t status;
    scmi_unpack_header(shmem->msg_header, &msg_id, &msg_type, &proto_id, &token);
    logger_info("SCMI Response Header: message_id=%d, message_type=%d\n", msg_id, msg_type);
    logger_info("SCMI Response Header: protocol_id=%d, token=%d\n", proto_id, token);

    logger_info("SCMI Response Length: %u\n", shmem->length);
    memcpy((void *) &status, (void *) (shmem_addr + sizeof(struct scmi_shared_mem)), sizeof(status));
    logger_info("SCMI Response Status: %d\n", status);



    // 5、读取响应 payload
    struct get_clock_name_pld resp_payload;
    logger_info("sizeof(resp_payload) = %u\n", sizeof(resp_payload));
    // 拷贝响应 payload
    memcpy((void *) &resp_payload,
           (void *) (shmem_addr + PAYLOAD_OFFSET),
           sizeof(resp_payload));

    logger_info("SCMI Clock get name Response: flags=0x%x\n", resp_payload.flags);
    logger_info("name: %s\n", resp_payload.name);
}
// ===================================  CLOCK_NAME_GET END =============================



// =================  4.6.2.8 CLOCK_CONFIG_SET =============================
/*
    Bits[31:1] 
    Bit[0] 
    uint32 attributes 
    Reserved, must be zero. 
    Enable/Disable: 
    If set to 1, the clock device is enabled. 
    If set to 0, the clock device is disabled. 
*/
struct config {
    uint32_t clock_id;
    uint32_t attr;
};

void
clock_config_set(uint32_t id) {
    logger("clock config set\n");
    // 1、准备消息头
    uint32_t hd = scmi_pack_header(CLOCK_CONFIG_SET,     // message_id
                                   0,                    // message_type
                                   SCMI_PROTOCOL_CLOCK,  // protocol_id
                                   6                     // token
    );

    // 2、准备payload
    struct config cfg = {
        .clock_id = id,
        .attr = 0x1,
    };
    int32_t  payload_size = sizeof(struct config);

    // 3、写入共享内存
    struct scmi_shared_mem *shmem = (struct scmi_shared_mem *) SCMI_SHMEM_BASE;
    void * shmem_addr = (void *) SCMI_SHMEM_BASE;
    shmem->channel_status         = 0;
    shmem->flags                  = 0;
    shmem->length                 = sizeof(hd) + payload_size;
    shmem->msg_header             = hd;
    // 写入 payload
    memcpy((void *) (shmem_addr + sizeof(struct scmi_shared_mem)), (void *) &cfg, payload_size);
    DSB_SY();

    // 3、发起SMC调用
    if (!do_smc()) {
        logger_error("SCMI: SMC call failed.\n");
        return;
    }

    // 4、读取响应头、响应长度、相应状态
    uint8_t  msg_id, msg_type, proto_id;
    uint16_t token;
    uint32_t status;
    scmi_unpack_header(shmem->msg_header, &msg_id, &msg_type, &proto_id, &token);
    logger_info("SCMI Response Header: message_id=%d, message_type=%d\n", msg_id, msg_type);
    logger_info("SCMI Response Header: protocol_id=%d, token=%d\n", proto_id, token);

    logger_info("SCMI Response Length: %u\n", shmem->length);
    memcpy((void *) &status, (void *) (shmem_addr + sizeof(struct scmi_shared_mem)), sizeof(status));
    logger_info("SCMI Response Status: %d\n", status);

}

// ================ CLOCK_CONFIG_SET  END =============================






// ====================  4.6.2.7 CLOCK_RATE_GET  =================

struct hertz {
    uint32_t low;
    uint32_t high;
};

void
clock_rate_get(uint32_t id) {
    logger("clock rate get\n");
    // 1、准备消息头
    uint32_t hd = scmi_pack_header(CLOCK_RATE_GET,     // message_id
                                   0,                    // message_type
                                   SCMI_PROTOCOL_CLOCK,  // protocol_id
                                   9                     // token
    );

    // 2、准备payload
    uint32_t clock_id     = id;  // 查询时钟ID
    int32_t  payload_size = sizeof(clock_id);


    // 3、写入共享内存
    struct scmi_shared_mem *shmem = (struct scmi_shared_mem *) SCMI_SHMEM_BASE;
    void * shmem_addr = (void *) SCMI_SHMEM_BASE;
    shmem->channel_status         = 0;
    shmem->flags                  = 0;
    shmem->length                 = sizeof(hd) + payload_size;
    shmem->msg_header             = hd;

    DSB_SY();

    // 写入 payload
    memcpy((void *) (shmem_addr + sizeof(struct scmi_shared_mem)), (void *) &clock_id, sizeof(clock_id));

    // 3、发起SMC调用
    if (!do_smc()) {
        logger_error("SCMI: SMC call failed.\n");
        return;
    }


    // 4、读取响应头、响应长度、相应状态
    uint8_t  msg_id, msg_type, proto_id;
    uint16_t token;
    uint32_t status;
    scmi_unpack_header(shmem->msg_header, &msg_id, &msg_type, &proto_id, &token);
    logger_info("SCMI Response Header: message_id=%d, message_type=%d\n", msg_id, msg_type);
    logger_info("SCMI Response Header: protocol_id=%d, token=%d\n", proto_id, token);

    logger_info("SCMI Response Length: %u\n", shmem->length);
    memcpy((void *) &status, (void *) (shmem_addr + sizeof(struct scmi_shared_mem)), sizeof(status));
    logger_info("SCMI Response Status: %d\n", status);

    // 5、读取响应 payload
    struct hertz hz;
    logger_info("sizeof(hertz) = %u\n", sizeof(struct hertz));
    // 拷贝响应 payload
    memcpy((void *) &hz,
           (void *) (shmem_addr + PAYLOAD_OFFSET),
           sizeof(hz));

    logger_info("got hz: %ld\n", *(uint64_t*)(void*)&hz);
    
}

// ====================  CLOCK_RATE_GET  END =================




// ============================== 4.6.2.6 CLOCK_RATE_SET  ====================
/*
Bits[31:4] 
Bits[3:2] 
Bit[1] 
uint32 flags 
Bit[0] 
Reserved, must be zero. 
Round up/down: 
If Bit[3] is set to 1, the platform rounds up/down 
autonomously to choose a physical rate closest 
to the requested rate, and Bit[2] is ignored. 
If Bit[3] is set to 0, the platform: 
• rounds up if Bit[2] is set to 1  
• rounds down if Bit[2] is set to 0 
Ignore delayed response: 
If the Async flag, bit[0], is set to 1 and this bit is 
set to 1, the platform does not send a 
CLOCK_RATE_SET delayed response. 
If the Async flag, bit[0], is set to 1 and this bit is 
set to 0, the platform does send a 
CLOCK_RATE_SET delayed response. 
If the Async flag, bit[0], is set to 0, then this bit 
field is ignored by the platform. 
Async flag: 
Set to 1 if clock rate is to be set asynchronously. 
In this case the call is completed with 
CLOCK_RATE_SET_COMPLETE message if 
bit[1] is set to 0. For more details, see section 
4.6.3.1. A SUCCESS return code in this case 
indicates that the platform has successfully 
queued this command. 
Set 0 to if the clock rate is to be set 
synchronously. In this case, the call will return 
when the clock rate setting has been completed.  
*/
struct rate_set_param {
    uint32_t flags;
    uint32_t clock_id;
    uint32_t hz_low;
    uint32_t hz_high;
};

void
clock_rate_set(uint32_t id)
{
    logger("clock rate set\n");
    // 1、准备消息头
    uint32_t hd = scmi_pack_header(CLOCK_RATE_SET,     // message_id
                                   0,                    // message_type
                                   SCMI_PROTOCOL_CLOCK,  // protocol_id
                                   7                     // token
    );

    // 2、准备payload
    struct rate_set_param cfg = {
        .flags = (0 << 0) | (0 << 1) | (0b11 << 2),
        .clock_id = id,
        .hz_low = 0xbebc200,
        .hz_high = 0,
    };
    int32_t  payload_size = sizeof(struct rate_set_param);

    // 3、写入共享内存
    struct scmi_shared_mem *shmem = (struct scmi_shared_mem *) SCMI_SHMEM_BASE;
    void * shmem_addr = (void *) SCMI_SHMEM_BASE;
    shmem->channel_status         = 0;
    shmem->flags                  = 0;
    shmem->length                 = sizeof(hd) + payload_size;
    shmem->msg_header             = hd;
    // 写入 payload
    memcpy((void *) (shmem_addr + sizeof(struct scmi_shared_mem)), (void *) &cfg, payload_size);
    DSB_SY();

    // 3、发起SMC调用
    if (!do_smc()) {
        logger_error("SCMI: SMC call failed.\n");
        return;
    }

    // 4、读取响应头、响应长度、相应状态
    uint8_t  msg_id, msg_type, proto_id;
    uint16_t token;
    int32_t status;
    scmi_unpack_header(shmem->msg_header, &msg_id, &msg_type, &proto_id, &token);
    logger_info("SCMI Response Header: message_id=%d, message_type=%d\n", msg_id, msg_type);
    logger_info("SCMI Response Header: protocol_id=%d, token=%d\n", proto_id, token);
    memcpy((void *) &status, (void *) (shmem_addr + sizeof(struct scmi_shared_mem)), sizeof(status));
    logger_info("SCMI Response Status: %d\n", status);

}


// ============================== CLOCK_RATE_SET  END ====================

void
enable_scmi_clock(uint32_t clock_id)
{
    logger("Enabling SCMI clock ID %u...\n", clock_id);

    protocto_version_check();

    clock_config_set(clock_id);  // open

    clock_rate_get(clock_id);

    clock_rate_set(clock_id);

    clock_rate_get(clock_id);

    logger("SCMI clock ID %u enabled.\n", clock_id);
}