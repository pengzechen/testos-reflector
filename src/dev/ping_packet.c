#include "lib/t_logger.h"
#include "mem/t_mmio.h"
#include "mem/t_mem.h"
#include "lib/t_string.h"
#include "mem/cache.h"
#include "t_types.h"
#include "dev/rtl8169.h"

static uint8_t           my_mac[ETH_ALEN]     = {0x2e, 0xc3, 0x69, 0x34, 0x7d, 0x31};
static uint8_t           remote_mac[ETH_ALEN] = {0x00, 0xe0, 0x1e, 0x1c, 0x01, 0x5e}; // 00:e0:1e:1c:01:5e

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