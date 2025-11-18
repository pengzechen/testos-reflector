#include "lib/t_logger.h"
#include "mem/t_mmio.h"
#include "mem/t_mem.h"
#include "lib/t_string.h"
#include "mem/cache.h"
#include "t_types.h"
#include "dev/rtl8169.h"

static uint8_t my_mac[ETH_ALEN]     = {0x2e, 0xc3, 0x69, 0x34, 0x7d, 0x30};
static uint8_t remote_mac[ETH_ALEN] = {0x38, 0xf7, 0xcd, 0xc8, 0xd9, 0x32};  // 38:f7:cd:c8:d9:32

/* Checksum calculation */
static uint16_t
ip_checksum(void *data, int len)
{
    uint32_t  sum = 0;
    uint16_t *ptr = (uint16_t *) data;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }

    if (len == 1) {
        sum += *(uint8_t *) ptr;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum;
}

/* Byte swap helpers */
static inline uint16_t
htons(uint16_t val)
{
    return ((val & 0xFF) << 8) | ((val >> 8) & 0xFF);
}

static inline uint32_t
htonl(uint32_t val)
{
    return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) | ((val >> 8) & 0xFF00) |
           ((val >> 24) & 0xFF);
}

static inline uint16_t
ntohs(uint16_t val)
{
    return htons(val);
}

static inline uint32_t
ntohl(uint32_t val)
{
    return htonl(val);
}


/**
 * send_ping - Send ICMP Echo Request
 */
uint32_t
generate_ping(uint8_t src_ip[4], uint8_t dst_ip[4], uint16_t seq, uint8_t packet[128])
{
    eth_hdr_t  *eth;
    ip_hdr_t   *ip;
    icmp_hdr_t *icmp;
    uint8_t    *payload;
    uint32_t    pkt_len = 0;

    logger_info("=== Preparing ICMP Echo Request (Ping) ===\n");
    logger_info("  Source IP: %d.%d.%d.%d\n", src_ip[0], src_ip[1], src_ip[2], src_ip[3]);
    logger_info("  Destination IP: %d.%d.%d.%d\n", dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
    logger_info("  Sequence: %d\n", seq);

    memset(packet, 0, sizeof(packet));

    /* Ethernet header */
    eth = (eth_hdr_t *) packet;
    /* Destination MAC (broadcast for simplicity) */
    memcpy(eth->dest, remote_mac, ETH_ALEN);
    memcpy(eth->src, my_mac, ETH_ALEN);
    eth->proto = htons(ETH_P_IP);
    pkt_len += sizeof(eth_hdr_t);

    logger_debug("  Ethernet header:\n");
    logger_debug("    Dest MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                 eth->dest[0],
                 eth->dest[1],
                 eth->dest[2],
                 eth->dest[3],
                 eth->dest[4],
                 eth->dest[5]);
    logger_debug("    Src MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                 my_mac[0],
                 my_mac[1],
                 my_mac[2],
                 my_mac[3],
                 my_mac[4],
                 my_mac[5]);
    logger_debug("    EtherType: 0x%04x (IP)\n", ETH_P_IP);

    /* IP header */
    ip              = (ip_hdr_t *) (packet + pkt_len);
    ip->version_ihl = 0x45; /* IPv4, 20-byte header */
    ip->tos         = 0;
    ip->total_len   = htons(sizeof(ip_hdr_t) + sizeof(icmp_hdr_t) + 32);
    ip->id          = htons(0x1234);
    ip->frag_off    = 0;
    ip->ttl         = 64;
    ip->protocol    = IPPROTO_ICMP;
    ip->checksum    = 0;
    memcpy(&ip->src_addr, src_ip, 4);
    memcpy(&ip->dest_addr, dst_ip, 4);
    ip->checksum = ip_checksum(ip, sizeof(ip_hdr_t));
    pkt_len += sizeof(ip_hdr_t);

    logger_debug("  IP header:\n");
    logger_debug("    Version: 4, Header length: 20 bytes\n");
    logger_debug("    Total length: %d bytes\n", ntohs(ip->total_len));
    logger_debug("    TTL: %d\n", ip->ttl);
    logger_debug("    Protocol: %d (ICMP)\n", ip->protocol);
    logger_debug("    Checksum: 0x%04x\n", ntohs(ip->checksum));

    /* ICMP header */
    icmp           = (icmp_hdr_t *) (packet + pkt_len);
    icmp->type     = ICMP_ECHO;
    icmp->code     = 0;
    icmp->checksum = 0;
    icmp->id       = htons(0x5678);
    icmp->sequence = htons(seq);
    pkt_len += sizeof(icmp_hdr_t);

    /* ICMP payload */
    payload = packet + pkt_len;
    for (int i = 0; i < 32; i++) {
        payload[i] = i;
    }
    pkt_len += 32;

    /* Calculate ICMP checksum */
    icmp->checksum = ip_checksum(icmp, sizeof(icmp_hdr_t) + 32);

    logger_debug("  ICMP header:\n");
    logger_debug("    Type: %d (Echo Request)\n", icmp->type);
    logger_debug("    Code: %d\n", icmp->code);
    logger_debug("    Checksum: 0x%04x\n", ntohs(icmp->checksum));
    logger_debug("    ID: 0x%04x\n", ntohs(icmp->id));
    logger_debug("    Sequence: %d\n", ntohs(icmp->sequence));

    logger_info("  Total packet size: %d bytes\n", pkt_len);
    return pkt_len;
}

/**
 * parse_packet - 解析接收到的网络包
 * @packet: 接收到的数据包
 * @length: 数据包长度
 * @my_mac: 本机MAC地址
 * @my_ip: 本机IP地址
 * 
 * 返回值:
 *   0: 不是给我们的包或无需处理
 *   1: ARP请求
 *   2: ICMP Echo请求(Ping)
 *   3: ICMP Echo回复(Ping Reply)
 *   -1: 解析失败
 */
int
parse_packet(unsigned char *packet, int length, uint8_t my_mac[ETH_ALEN], uint8_t my_ip[4])
{
    eth_hdr_t  *eth;
    arp_hdr_t  *arp;
    ip_hdr_t   *ip;
    icmp_hdr_t *icmp;

    if (length < sizeof(eth_hdr_t)) {
        logger_debug("Packet too short for Ethernet header\n");
        return -1;
    }

    eth = (eth_hdr_t *) packet;

    /* 检查是否是发给我们的包(广播或单播到我们的MAC) */
    int is_broadcast = 1;
    for (int i = 0; i < ETH_ALEN; i++) {
        if (eth->dest[i] != 0xFF) {
            is_broadcast = 0;
            break;
        }
    }

    int is_for_us = is_broadcast;
    if (!is_broadcast) {
        is_for_us = 1;
        for (int i = 0; i < ETH_ALEN; i++) {
            if (eth->dest[i] != my_mac[i]) {
                is_for_us = 0;
                break;
            }
        }
    }

    if (!is_for_us) {
        logger_debug("Packet not for us, ignoring\n");
        return 0;
    }

    uint16_t proto = ntohs(eth->proto);
    logger_info("=== Received Packet ===\n");
    logger_info("  EtherType: 0x%04x\n", proto);
    logger_info("  Dest MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                eth->dest[0],
                eth->dest[1],
                eth->dest[2],
                eth->dest[3],
                eth->dest[4],
                eth->dest[5]);
    logger_info("  Src MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                eth->src[0],
                eth->src[1],
                eth->src[2],
                eth->src[3],
                eth->src[4],
                eth->src[5]);

    /* 处理ARP包 */
    if (proto == ETH_P_ARP) {
        if (length < sizeof(eth_hdr_t) + sizeof(arp_hdr_t)) {
            logger_debug("Packet too short for ARP\n");
            return -1;
        }

        arp             = (arp_hdr_t *) (packet + sizeof(eth_hdr_t));
        uint16_t opcode = ntohs(arp->opcode);

        logger_info("  ARP Opcode: %d (%s)\n",
                    opcode,
                    opcode == ARP_REQUEST ? "Request"
                    : opcode == ARP_REPLY ? "Reply"
                                          : "Unknown");
        logger_info("  Sender IP: %d.%d.%d.%d\n",
                    arp->sender_ip[0],
                    arp->sender_ip[1],
                    arp->sender_ip[2],
                    arp->sender_ip[3]);
        logger_info("  Target IP: %d.%d.%d.%d\n",
                    arp->target_ip[0],
                    arp->target_ip[1],
                    arp->target_ip[2],
                    arp->target_ip[3]);

        /* 检查是否是请求我们的IP */
        if (opcode == ARP_REQUEST) {
            int target_is_us = 1;
            for (int i = 0; i < 4; i++) {
                if (arp->target_ip[i] != my_ip[i]) {
                    target_is_us = 0;
                    break;
                }
            }
            if (target_is_us) {
                logger_info("  => ARP Request for our IP, need to reply\n");
                return 1;
            }
        }
        return 0;
    }

    /* 处理IP包 */
    if (proto == ETH_P_IP) {
        if (length < sizeof(eth_hdr_t) + sizeof(ip_hdr_t)) {
            logger_debug("Packet too short for IP header\n");
            return -1;
        }

        ip = (ip_hdr_t *) (packet + sizeof(eth_hdr_t));

        logger_info("  IP Protocol: %d\n", ip->protocol);
        logger_info("  Src IP: %d.%d.%d.%d\n",
                    (ntohl(ip->src_addr) >> 24) & 0xFF,
                    (ntohl(ip->src_addr) >> 16) & 0xFF,
                    (ntohl(ip->src_addr) >> 8) & 0xFF,
                    ntohl(ip->src_addr) & 0xFF);
        logger_info("  Dst IP: %d.%d.%d.%d\n",
                    (ntohl(ip->dest_addr) >> 24) & 0xFF,
                    (ntohl(ip->dest_addr) >> 16) & 0xFF,
                    (ntohl(ip->dest_addr) >> 8) & 0xFF,
                    ntohl(ip->dest_addr) & 0xFF);

        /* 检查目标IP是否是我们 */
        uint32_t my_ip_val = (my_ip[0] << 24) | (my_ip[1] << 16) | (my_ip[2] << 8) | my_ip[3];
        if (ntohl(ip->dest_addr) != my_ip_val) {
            logger_debug("IP packet not for us\n");
            return 0;
        }

        /* 处理ICMP包 */
        if (ip->protocol == IPPROTO_ICMP) {
            if (length < sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + sizeof(icmp_hdr_t)) {
                logger_debug("Packet too short for ICMP\n");
                return -1;
            }

            icmp = (icmp_hdr_t *) (packet + sizeof(eth_hdr_t) + sizeof(ip_hdr_t));

            logger_info("  ICMP Type: %d\n", icmp->type);
            logger_info("  ICMP Code: %d\n", icmp->code);

            if (icmp->type == ICMP_ECHO) {
                logger_info("  => ICMP Echo Request (Ping), need to reply\n");
                return 2;
            } else if (icmp->type == ICMP_ECHOREPLY) {
                logger_info("  => ICMP Echo Reply (Ping response)\n");
                logger_info("  Sequence: %d\n", ntohs(icmp->sequence));
                return 3; /* 返回3表示收到ping回复 */
            }
        }
    }

    return 0;
}

/**
 * process_arp_request - 处理ARP请求并生成响应
 * @packet: 接收到的ARP请求包
 * @length: 数据包长度
 * @my_mac: 本机MAC地址
 * @my_ip: 本机IP地址
 * @reply_packet: 用于存储响应包的缓冲区
 * 
 * 返回值: 响应包的长度,如果出错返回负值
 */
int
process_arp_request(unsigned char *packet,
                    int            length,
                    uint8_t        my_mac[ETH_ALEN],
                    uint8_t        my_ip[4],
                    unsigned char *reply_packet)
{
    eth_hdr_t *recv_eth, *send_eth;
    arp_hdr_t *recv_arp, *send_arp;

    if (length < sizeof(eth_hdr_t) + sizeof(arp_hdr_t)) {
        logger_error("Invalid ARP packet length\n");
        return -1;
    }

    recv_eth = (eth_hdr_t *) packet;
    recv_arp = (arp_hdr_t *) (packet + sizeof(eth_hdr_t));

    logger_info("=== Generating ARP Reply ===\n");

    /* 构造以太网头 */
    send_eth = (eth_hdr_t *) reply_packet;
    memcpy(send_eth->dest, recv_arp->sender_mac, ETH_ALEN);
    memcpy(send_eth->src, my_mac, ETH_ALEN);
    send_eth->proto = htons(ETH_P_ARP);

    /* 构造ARP响应 */
    send_arp                 = (arp_hdr_t *) (reply_packet + sizeof(eth_hdr_t));
    send_arp->hw_type        = htons(ARP_HW_TYPE_ETHERNET);
    send_arp->proto_type     = htons(ETH_P_IP);
    send_arp->hw_addr_len    = ETH_ALEN;
    send_arp->proto_addr_len = 4;
    send_arp->opcode         = htons(ARP_REPLY);

    /* 填充MAC和IP地址 */
    memcpy(send_arp->sender_mac, my_mac, ETH_ALEN);
    memcpy(send_arp->sender_ip, my_ip, 4);
    memcpy(send_arp->target_mac, recv_arp->sender_mac, ETH_ALEN);
    memcpy(send_arp->target_ip, recv_arp->sender_ip, 4);

    logger_info("  Target MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                send_arp->target_mac[0],
                send_arp->target_mac[1],
                send_arp->target_mac[2],
                send_arp->target_mac[3],
                send_arp->target_mac[4],
                send_arp->target_mac[5]);
    logger_info("  Target IP: %d.%d.%d.%d\n",
                send_arp->target_ip[0],
                send_arp->target_ip[1],
                send_arp->target_ip[2],
                send_arp->target_ip[3]);

    return sizeof(eth_hdr_t) + sizeof(arp_hdr_t);
}

/**
 * process_ping_request - 处理Ping请求并生成响应
 * @packet: 接收到的Ping请求包
 * @length: 数据包长度
 * @my_mac: 本机MAC地址
 * @my_ip: 本机IP地址
 * @reply_packet: 用于存储响应包的缓冲区
 * 
 * 返回值: 响应包的长度,如果出错返回负值
 */
int
process_ping_request(unsigned char *packet,
                     int            length,
                     uint8_t        my_mac[ETH_ALEN],
                     uint8_t        my_ip[4],
                     unsigned char *reply_packet)
{
    eth_hdr_t  *recv_eth, *send_eth;
    ip_hdr_t   *recv_ip, *send_ip;
    icmp_hdr_t *recv_icmp, *send_icmp;
    uint8_t    *payload;
    int         ip_header_len;
    int         icmp_data_len;

    if (length < sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + sizeof(icmp_hdr_t)) {
        logger_error("Invalid ICMP packet length\n");
        return -1;
    }

    recv_eth      = (eth_hdr_t *) packet;
    recv_ip       = (ip_hdr_t *) (packet + sizeof(eth_hdr_t));
    ip_header_len = (recv_ip->version_ihl & 0x0F) * 4;
    recv_icmp     = (icmp_hdr_t *) (packet + sizeof(eth_hdr_t) + ip_header_len);

    logger_info("=== Generating ICMP Echo Reply (Ping Response) ===\n");

    /* 计算ICMP数据长度 */
    icmp_data_len = ntohs(recv_ip->total_len) - ip_header_len - sizeof(icmp_hdr_t);

    /* 构造以太网头 */
    send_eth = (eth_hdr_t *) reply_packet;
    memcpy(send_eth->dest, recv_eth->src, ETH_ALEN);
    memcpy(send_eth->src, my_mac, ETH_ALEN);
    send_eth->proto = htons(ETH_P_IP);

    /* 构造IP头 */
    send_ip              = (ip_hdr_t *) (reply_packet + sizeof(eth_hdr_t));
    send_ip->version_ihl = 0x45;
    send_ip->tos         = 0;
    send_ip->total_len   = htons(sizeof(ip_hdr_t) + sizeof(icmp_hdr_t) + icmp_data_len);
    send_ip->id          = recv_ip->id;
    send_ip->frag_off    = 0;
    send_ip->ttl         = 64;
    send_ip->protocol    = IPPROTO_ICMP;
    send_ip->checksum    = 0;
    send_ip->src_addr    = recv_ip->dest_addr;
    send_ip->dest_addr   = recv_ip->src_addr;
    send_ip->checksum    = ip_checksum(send_ip, sizeof(ip_hdr_t));

    /* 构造ICMP头 */
    send_icmp           = (icmp_hdr_t *) (reply_packet + sizeof(eth_hdr_t) + sizeof(ip_hdr_t));
    send_icmp->type     = ICMP_ECHOREPLY;
    send_icmp->code     = 0;
    send_icmp->checksum = 0;
    send_icmp->id       = recv_icmp->id;
    send_icmp->sequence = recv_icmp->sequence;

    /* 复制ICMP数据 */
    payload = reply_packet + sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + sizeof(icmp_hdr_t);
    memcpy(payload, packet + sizeof(eth_hdr_t) + ip_header_len + sizeof(icmp_hdr_t), icmp_data_len);

    /* 计算ICMP校验和 */
    send_icmp->checksum = ip_checksum(send_icmp, sizeof(icmp_hdr_t) + icmp_data_len);

    logger_info("  Reply to: %d.%d.%d.%d\n",
                (ntohl(send_ip->dest_addr) >> 24) & 0xFF,
                (ntohl(send_ip->dest_addr) >> 16) & 0xFF,
                (ntohl(send_ip->dest_addr) >> 8) & 0xFF,
                ntohl(send_ip->dest_addr) & 0xFF);
    logger_info("  Sequence: %d\n", ntohs(send_icmp->sequence));

    return sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + sizeof(icmp_hdr_t) + icmp_data_len;
}