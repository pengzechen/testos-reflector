
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


#define SCMI_SMC_CALL       0x82000010  // 根据你的 DTS 设置，SCMI SMC ID

enum scmi_std_protocol {
        SCMI_PROTOCOL_BASE = 0x10,
        SCMI_PROTOCOL_POWER = 0x11,
        SCMI_PROTOCOL_SYSTEM = 0x12,
        SCMI_PROTOCOL_PERF = 0x13,
        SCMI_PROTOCOL_CLOCK = 0x14,
        SCMI_PROTOCOL_SENSOR = 0x15,
        SCMI_PROTOCOL_RESET = 0x16,
        SCMI_PROTOCOL_VOLTAGE = 0x17,
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

struct scmi_msg {
	void *buf;
	size_t len;
};

/**
 * struct scmi_msg_hdr - Message(Tx/Rx) header
 *
 * @id: The identifier of the message being sent
 * @protocol_id: The identifier of the protocol used to send @id message
 * @type: The SCMI type for this message
 * @seq: The token to identify the message. When a message returns, the
 *	platform returns the whole message header unmodified including the
 *	token
 * @status: Status of the transfer once it's complete
 * @poll_completion: Indicate if the transfer needs to be polled for
 *	completion or interrupt mode is used
 */
struct scmi_msg_hdr {
	uint8_t id;
	uint8_t protocol_id;
	uint8_t type;
	uint16_t seq;
	uint32_t status;
	bool poll_completion;
};


#endif  // __SCMI_H__