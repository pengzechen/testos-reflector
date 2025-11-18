#ifndef DEV_RTL8169_H
#define DEV_RTL8169_H

/* Ethernet & IP protocol constants */
#define ETH_ALEN       6
#define ETH_HLEN       14
#define ETH_P_IP       0x0800
#define ETH_P_ARP      0x0806
#define IPPROTO_ICMP   1
#define ICMP_ECHO      8
#define ICMP_ECHOREPLY 0

/* Network structures */
typedef struct
{
    uint8_t  dest[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t proto;
} __attribute__((packed)) eth_hdr_t;

typedef struct
{
    uint8_t  version_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_addr;
    uint32_t dest_addr;
} __attribute__((packed)) ip_hdr_t;

typedef struct
{
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed)) icmp_hdr_t;

typedef struct
{
    uint32_t status;
    uint32_t vlan_tag;
    uint32_t buf_addr_lo;
    uint32_t buf_addr_hi;
} __attribute__((packed)) rtl_desc_t;

typedef struct pci_child_plat
{
    uint16_t vendor;
    uint16_t device;
};

struct udevice
{
    char          name[32];  // 设备名称
    unsigned long mmio_base;
};

uint32_t generate_ping(uint8_t src_ip[4], uint8_t dst_ip[4], uint16_t seq, uint8_t packet[128]);
int rtl8169_eth_probe(struct udevice *dev);
void rtl8169_eth_stop(struct udevice *dev);
int rtl8169_eth_start(struct udevice *dev);
int rtl8169_eth_send(struct udevice *dev, void *packet, int length);
int rtl8169_eth_recv(struct udevice *dev, int flags, unsigned char **packetp);
void test_rtl8125(void);
#endif
