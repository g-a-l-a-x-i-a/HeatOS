#include "network.h"
#include "io.h"
#include "string.h"

/* PCI config mechanism #1 */
#define PCI_CFG_ADDR 0xCF8
#define PCI_CFG_DATA 0xCFC

/* NE2000 register offsets */
#define NE_CR       0x00
#define NE_PSTART   0x01
#define NE_PSTOP    0x02
#define NE_BNRY     0x03
#define NE_TPSR     0x04
#define NE_TBCR0    0x05
#define NE_TBCR1    0x06
#define NE_ISR      0x07
#define NE_RSAR0    0x08
#define NE_RSAR1    0x09
#define NE_RBCR0    0x0A
#define NE_RBCR1    0x0B
#define NE_RCR      0x0C
#define NE_TCR      0x0D
#define NE_DCR      0x0E
#define NE_IMR      0x0F

#define NE_DATA     0x10
#define NE_RESET    0x1F

/* Page 1 overlays */
#define NE_P1_PAR0  0x01
#define NE_P1_CURR  0x07
#define NE_P1_MAR0  0x08

/* CR bits */
#define NE_CR_STP   0x01
#define NE_CR_STA   0x02
#define NE_CR_TXP   0x04
#define NE_CR_RD0   0x08
#define NE_CR_RD1   0x10
#define NE_CR_RD2   0x20
#define NE_CR_PS0   0x00
#define NE_CR_PS1   0x40

/* ISR bits */
#define NE_ISR_PRX  0x01
#define NE_ISR_PTX  0x02
#define NE_ISR_RXE  0x04
#define NE_ISR_TXE  0x08
#define NE_ISR_OVW  0x10
#define NE_ISR_CNT  0x20
#define NE_ISR_RDC  0x40
#define NE_ISR_RST  0x80

/* Ring layout */
#define NE_TX_START_PAGE 0x40
#define NE_RX_START_PAGE 0x46
#define NE_RX_STOP_PAGE  0x80

#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_ARP  0x0806

#define ARP_HTYPE_ETH 0x0001
#define ARP_OP_REQ    0x0001
#define ARP_OP_REPLY  0x0002

#define IPV4_PROTO_ICMP 1
#define IPV4_PROTO_UDP  17
#define DNS_PORT         53
#define DNS_SRC_PORT_MIN 53000
#define DNS_SRC_PORT_MAX 53999
#define DNS_MAX_PACKET   512
#define PING_LOOPS_PER_MS 6000u

static bool     g_probe_done = false;
static bool     g_present = false;
static bool     g_ready = false;
static uint8_t  g_pci_slot = 0;
static uint16_t g_vendor_id = 0;
static uint16_t g_device_id = 0;
static uint16_t g_io_base = 0;

static uint8_t g_mac[6] = {0, 0, 0, 0, 0, 0};
static uint8_t g_ip[4] = {10, 0, 2, 15};
static uint8_t g_gateway[4] = {10, 0, 2, 2};
static uint8_t g_netmask[4] = {255, 255, 255, 0};

static bool    g_arp_valid = false;
static uint8_t g_arp_ip[4] = {0, 0, 0, 0};
static uint8_t g_arp_mac[6] = {0, 0, 0, 0, 0, 0};

static bool     g_ping_waiting = false;
static bool     g_ping_reply = false;
static uint8_t  g_ping_reply_ttl = 0;
static uint16_t g_ping_reply_bytes = 0;
static uint16_t g_ping_id = 0x484Fu;
static uint16_t g_ping_expected_seq = 0;
static uint16_t g_ping_seq = 1;
static uint8_t  g_ping_target[4] = {0, 0, 0, 0};

static uint16_t g_ipv4_id = 1;

static const uint8_t g_dns_server[4] = {10, 0, 2, 3};
static bool     g_dns_waiting = false;
static bool     g_dns_reply = false;
static bool     g_dns_reply_has_a = false;
static uint8_t  g_dns_reply_ip[4] = {0, 0, 0, 0};
static uint16_t g_dns_expected_id = 0;
static uint16_t g_dns_expected_src_port = 0;
static uint16_t g_dns_src_port = DNS_SRC_PORT_MIN;
static uint16_t g_dns_next_id = 1;

static uint8_t g_rx_buf[1600];
static uint8_t g_tx_buf[1600];

static bool bytes_equal(const uint8_t *a, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static uint16_t rd16be(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static void wr16be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t checksum16(const uint8_t *data, uint16_t len) {
    uint32_t sum = 0;
    uint16_t i = 0;

    while ((uint16_t)(i + 1) < len) {
        sum += (uint32_t)rd16be(&data[i]);
        i = (uint16_t)(i + 2);
    }

    if (i < len) {
        sum += (uint32_t)((uint16_t)data[i] << 8);
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

static char to_lower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static bool host_char_ok(char c) {
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= '0' && c <= '9') return true;
    return c == '-';
}

static bool dns_skip_name(const uint8_t *msg, uint16_t msg_len, uint16_t *off_io) {
    uint16_t off = *off_io;
    uint16_t guard = 0;

    while (off < msg_len) {
        uint8_t len = msg[off];
        if (len == 0) {
            off++;
            *off_io = off;
            return true;
        }

        if ((len & 0xC0u) == 0xC0u) {
            if ((uint16_t)(off + 1u) >= msg_len) return false;
            off += 2;
            *off_io = off;
            return true;
        }

        if (len & 0xC0u) {
            return false;
        }

        off++;
        if ((uint16_t)(off + len) > msg_len) {
            return false;
        }
        off = (uint16_t)(off + len);

        if (++guard > msg_len) {
            return false;
        }
    }

    return false;
}

static bool build_dns_query(const char *host, uint16_t id, uint8_t *out, uint16_t out_max, uint16_t *out_len) {
    uint16_t pos = 12;
    uint16_t label_len_pos = 0;
    uint16_t label_len = 0;
    const char *p = host;

    if (!host || !out || !out_len || out_max < 18u) {
        return false;
    }

    while (*p == ' ') p++;
    if (!*p) return false;

    memset(out, 0, out_max);
    wr16be(&out[0], id);
    wr16be(&out[2], 0x0100);   /* recursion desired */
    wr16be(&out[4], 1);        /* QDCOUNT */

    if (pos >= out_max) return false;
    label_len_pos = pos;
    out[pos++] = 0;

    while (*p) {
        char c = *p;

        if (c == '.') {
            if (label_len == 0) {
                return false;
            }
            out[label_len_pos] = (uint8_t)label_len;
            label_len = 0;

            if (p[1] == '\0') {
                p++;
                break;
            }

            if (pos >= out_max) return false;
            label_len_pos = pos;
            out[pos++] = 0;
            p++;
            continue;
        }

        if (!host_char_ok(c)) {
            return false;
        }

        if (label_len >= 63) {
            return false;
        }

        if (pos >= out_max) {
            return false;
        }

        out[pos++] = (uint8_t)to_lower_ascii(c);
        label_len++;
        p++;
    }

    if (label_len == 0) {
        return false;
    }
    out[label_len_pos] = (uint8_t)label_len;

    if ((uint16_t)(pos + 5u) > out_max) {
        return false;
    }

    out[pos++] = 0;            /* end of QNAME */
    wr16be(&out[pos], 1);      /* QTYPE A */
    pos = (uint16_t)(pos + 2);
    wr16be(&out[pos], 1);      /* QCLASS IN */
    pos = (uint16_t)(pos + 2);

    *out_len = pos;
    return true;
}

static uint32_t pci_read32_bus0(uint8_t slot, uint8_t reg) {
    uint32_t addr = 0x80000000u | ((uint32_t)slot << 11) | ((uint32_t)reg & 0xFCu);
    outl(PCI_CFG_ADDR, addr);
    return inl(PCI_CFG_DATA);
}

static void pci_write32_bus0(uint8_t slot, uint8_t reg, uint32_t value) {
    uint32_t addr = 0x80000000u | ((uint32_t)slot << 11) | ((uint32_t)reg & 0xFCu);
    outl(PCI_CFG_ADDR, addr);
    outl(PCI_CFG_DATA, value);
}

static bool probe_ne2000_pci(void) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        uint32_t id = pci_read32_bus0(slot, 0x00);
        uint16_t vendor = (uint16_t)(id & 0xFFFFu);
        uint16_t device = (uint16_t)(id >> 16);

        if (vendor == 0xFFFFu) {
            continue;
        }

        uint32_t class_reg = pci_read32_bus0(slot, 0x08);
        uint8_t class_code = (uint8_t)(class_reg >> 24);

        if (class_code != 0x02u) {
            continue;
        }

        uint32_t bar0 = pci_read32_bus0(slot, 0x10);
        if ((bar0 & 0x1u) == 0) {
            continue;
        }

        g_pci_slot = slot;
        g_vendor_id = vendor;
        g_device_id = device;
        g_io_base = (uint16_t)(bar0 & 0xFFFCu);

        /* Enable IO space + bus master. */
        uint32_t cmd = pci_read32_bus0(slot, 0x04);
        cmd |= 0x00000005u;
        pci_write32_bus0(slot, 0x04, cmd);

        g_present = true;
        return true;
    }

    g_present = false;
    return false;
}

static inline uint16_t ne_port(uint8_t reg) {
    return (uint16_t)(g_io_base + reg);
}

static inline void ne_write(uint8_t reg, uint8_t val) {
    outb(ne_port(reg), val);
}

static inline uint8_t ne_read(uint8_t reg) {
    return inb(ne_port(reg));
}

static bool ne_wait_isr(uint8_t mask, uint32_t guard) {
    while (guard--) {
        if (ne_read(NE_ISR) & mask) {
            return true;
        }
    }
    return false;
}

static bool ne_dma_read(uint16_t addr, uint8_t *dst, uint16_t len) {
    if (len == 0) return true;

    uint16_t xfer_len = (uint16_t)((len + 1u) & ~1u);

    ne_write(NE_CR, (uint8_t)(NE_CR_STA | NE_CR_RD2 | NE_CR_PS0));
    ne_write(NE_RBCR0, (uint8_t)(xfer_len & 0xFFu));
    ne_write(NE_RBCR1, (uint8_t)(xfer_len >> 8));
    ne_write(NE_RSAR0, (uint8_t)(addr & 0xFFu));
    ne_write(NE_RSAR1, (uint8_t)(addr >> 8));
    ne_write(NE_ISR, NE_ISR_RDC);

    ne_write(NE_CR, (uint8_t)(NE_CR_STA | NE_CR_RD0 | NE_CR_PS0));

    for (uint16_t i = 0; i < xfer_len; i = (uint16_t)(i + 2)) {
        uint16_t w = inw(ne_port(NE_DATA));
        if (i < len) {
            dst[i] = (uint8_t)(w & 0xFFu);
        }
        if ((uint16_t)(i + 1) < len) {
            dst[i + 1] = (uint8_t)(w >> 8);
        }
    }

    if (!ne_wait_isr(NE_ISR_RDC, 200000u)) {
        return false;
    }

    ne_write(NE_ISR, NE_ISR_RDC);
    return true;
}

static bool ne_dma_write(uint16_t addr, const uint8_t *src, uint16_t len) {
    if (len == 0) return true;

    uint16_t xfer_len = (uint16_t)((len + 1u) & ~1u);

    ne_write(NE_CR, (uint8_t)(NE_CR_STA | NE_CR_RD2 | NE_CR_PS0));
    ne_write(NE_RBCR0, (uint8_t)(xfer_len & 0xFFu));
    ne_write(NE_RBCR1, (uint8_t)(xfer_len >> 8));
    ne_write(NE_RSAR0, (uint8_t)(addr & 0xFFu));
    ne_write(NE_RSAR1, (uint8_t)(addr >> 8));
    ne_write(NE_ISR, NE_ISR_RDC);

    ne_write(NE_CR, (uint8_t)(NE_CR_STA | NE_CR_RD1 | NE_CR_PS0));

    for (uint16_t i = 0; i < xfer_len; i = (uint16_t)(i + 2)) {
        uint16_t w = 0;
        if (i < len) {
            w = src[i];
        }
        if ((uint16_t)(i + 1) < len) {
            w |= (uint16_t)((uint16_t)src[i + 1] << 8);
        }
        outw(ne_port(NE_DATA), w);
    }

    if (!ne_wait_isr(NE_ISR_RDC, 200000u)) {
        return false;
    }

    ne_write(NE_ISR, NE_ISR_RDC);
    return true;
}

static bool ne_send_frame(const uint8_t *frame, uint16_t len) {
    if (!g_ready || !frame || len < 14) return false;

    uint16_t tx_len = (len < 60u) ? 60u : len;
    if (tx_len > (uint16_t)sizeof(g_tx_buf)) return false;

    memcpy(g_tx_buf, frame, len);
    if (tx_len > len) {
        memset(&g_tx_buf[len], 0, (size_t)(tx_len - len));
    }

    if (!ne_dma_write((uint16_t)((uint16_t)NE_TX_START_PAGE << 8), g_tx_buf, tx_len)) {
        return false;
    }

    ne_write(NE_TPSR, NE_TX_START_PAGE);
    ne_write(NE_TBCR0, (uint8_t)(tx_len & 0xFFu));
    ne_write(NE_TBCR1, (uint8_t)(tx_len >> 8));
    ne_write(NE_ISR, (uint8_t)(NE_ISR_PTX | NE_ISR_TXE));
    ne_write(NE_CR, (uint8_t)(NE_CR_STA | NE_CR_RD2 | NE_CR_TXP | NE_CR_PS0));

    for (uint32_t guard = 0; guard < 300000u; guard++) {
        uint8_t isr = ne_read(NE_ISR);
        if (isr & (uint8_t)(NE_ISR_PTX | NE_ISR_TXE)) {
            ne_write(NE_ISR, (uint8_t)(NE_ISR_PTX | NE_ISR_TXE));
            return (isr & NE_ISR_PTX) != 0;
        }
    }

    return false;
}

static uint8_t ne_curr_page(void) {
    ne_write(NE_CR, (uint8_t)(NE_CR_STA | NE_CR_RD2 | NE_CR_PS1));
    uint8_t curr = ne_read(NE_P1_CURR);
    ne_write(NE_CR, (uint8_t)(NE_CR_STA | NE_CR_RD2 | NE_CR_PS0));
    return curr;
}

static bool is_broadcast_mac(const uint8_t mac[6]) {
    return mac[0] == 0xFFu && mac[1] == 0xFFu && mac[2] == 0xFFu &&
           mac[3] == 0xFFu && mac[4] == 0xFFu && mac[5] == 0xFFu;
}

static void build_arp_frame(uint8_t *buf, const uint8_t dst_mac[6], uint16_t op,
                            const uint8_t sender_mac[6], const uint8_t sender_ip[4],
                            const uint8_t target_mac[6], const uint8_t target_ip[4]) {
    memcpy(&buf[0], dst_mac, 6);
    memcpy(&buf[6], sender_mac, 6);
    wr16be(&buf[12], ETH_TYPE_ARP);

    wr16be(&buf[14], ARP_HTYPE_ETH);
    wr16be(&buf[16], ETH_TYPE_IPV4);
    buf[18] = 6;
    buf[19] = 4;
    wr16be(&buf[20], op);

    memcpy(&buf[22], sender_mac, 6);
    memcpy(&buf[28], sender_ip, 4);
    memcpy(&buf[32], target_mac, 6);
    memcpy(&buf[38], target_ip, 4);
}

static void send_arp_reply(const uint8_t dst_mac[6], const uint8_t dst_ip[4]) {
    uint8_t frame[42];
    build_arp_frame(frame, dst_mac, ARP_OP_REPLY, g_mac, g_ip, dst_mac, dst_ip);
    (void)ne_send_frame(frame, (uint16_t)sizeof(frame));
}

static void send_arp_request(const uint8_t target_ip[4]) {
    static const uint8_t k_broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t k_zero_mac[6] = {0, 0, 0, 0, 0, 0};

    uint8_t frame[42];
    build_arp_frame(frame, k_broadcast, ARP_OP_REQ, g_mac, g_ip, k_zero_mac, target_ip);
    (void)ne_send_frame(frame, (uint16_t)sizeof(frame));
}

static bool ip_same_subnet(const uint8_t a[4], const uint8_t b[4]) {
    for (int i = 0; i < 4; i++) {
        if ((a[i] & g_netmask[i]) != (b[i] & g_netmask[i])) {
            return false;
        }
    }
    return true;
}

static void route_next_hop(const uint8_t dst_ip[4], uint8_t next_hop[4]) {
    if (ip_same_subnet(dst_ip, g_ip)) {
        memcpy(next_hop, dst_ip, 4);
    } else {
        memcpy(next_hop, g_gateway, 4);
    }
}

static bool send_icmp_packet(const uint8_t dst_mac[6], const uint8_t dst_ip[4],
                             uint8_t icmp_type, uint16_t ident, uint16_t seq,
                             const uint8_t *payload, uint16_t payload_len) {
    uint16_t ip_len = (uint16_t)(20u + 8u + payload_len);
    uint16_t frame_len = (uint16_t)(14u + ip_len);

    if (frame_len > (uint16_t)sizeof(g_tx_buf)) {
        return false;
    }

    uint8_t *eth = g_tx_buf;
    uint8_t *ip = &g_tx_buf[14];
    uint8_t *icmp = &g_tx_buf[34];

    memcpy(&eth[0], dst_mac, 6);
    memcpy(&eth[6], g_mac, 6);
    wr16be(&eth[12], ETH_TYPE_IPV4);

    ip[0] = 0x45;
    ip[1] = 0x00;
    wr16be(&ip[2], ip_len);
    wr16be(&ip[4], g_ipv4_id++);
    wr16be(&ip[6], 0x0000);
    ip[8] = 64;
    ip[9] = IPV4_PROTO_ICMP;
    ip[10] = 0;
    ip[11] = 0;
    memcpy(&ip[12], g_ip, 4);
    memcpy(&ip[16], dst_ip, 4);
    wr16be(&ip[10], checksum16(ip, 20));

    icmp[0] = icmp_type;
    icmp[1] = 0;
    icmp[2] = 0;
    icmp[3] = 0;
    wr16be(&icmp[4], ident);
    wr16be(&icmp[6], seq);
    if (payload_len) {
        memcpy(&icmp[8], payload, payload_len);
    }
    wr16be(&icmp[2], checksum16(icmp, (uint16_t)(8u + payload_len)));

    return ne_send_frame(g_tx_buf, frame_len);
}

static bool send_udp_packet(const uint8_t dst_mac[6], const uint8_t dst_ip[4],
                            uint16_t src_port, uint16_t dst_port,
                            const uint8_t *payload, uint16_t payload_len) {
    uint16_t udp_len = (uint16_t)(8u + payload_len);
    uint16_t ip_len = (uint16_t)(20u + udp_len);
    uint16_t frame_len = (uint16_t)(14u + ip_len);

    if (frame_len > (uint16_t)sizeof(g_tx_buf)) {
        return false;
    }

    uint8_t *eth = g_tx_buf;
    uint8_t *ip = &g_tx_buf[14];
    uint8_t *udp = &g_tx_buf[34];

    memcpy(&eth[0], dst_mac, 6);
    memcpy(&eth[6], g_mac, 6);
    wr16be(&eth[12], ETH_TYPE_IPV4);

    ip[0] = 0x45;
    ip[1] = 0x00;
    wr16be(&ip[2], ip_len);
    wr16be(&ip[4], g_ipv4_id++);
    wr16be(&ip[6], 0x0000);
    ip[8] = 64;
    ip[9] = IPV4_PROTO_UDP;
    ip[10] = 0;
    ip[11] = 0;
    memcpy(&ip[12], g_ip, 4);
    memcpy(&ip[16], dst_ip, 4);
    wr16be(&ip[10], checksum16(ip, 20));

    wr16be(&udp[0], src_port);
    wr16be(&udp[2], dst_port);
    wr16be(&udp[4], udp_len);
    wr16be(&udp[6], 0x0000);   /* UDP checksum optional for IPv4 */

    if (payload_len) {
        memcpy(&udp[8], payload, payload_len);
    }

    return ne_send_frame(g_tx_buf, frame_len);
}

static void handle_arp_packet(const uint8_t *frame, uint16_t len) {
    if (len < 28) return;
    if (rd16be(&frame[0]) != ARP_HTYPE_ETH) return;
    if (rd16be(&frame[2]) != ETH_TYPE_IPV4) return;
    if (frame[4] != 6 || frame[5] != 4) return;

    uint16_t op = rd16be(&frame[6]);
    const uint8_t *sender_mac = &frame[8];
    const uint8_t *sender_ip = &frame[14];
    const uint8_t *target_ip = &frame[24];

    if (op == ARP_OP_REPLY) {
        if (bytes_equal(target_ip, g_ip, 4)) {
            memcpy(g_arp_ip, sender_ip, 4);
            memcpy(g_arp_mac, sender_mac, 6);
            g_arp_valid = true;
        }
        return;
    }

    if (op == ARP_OP_REQ && bytes_equal(target_ip, g_ip, 4)) {
        send_arp_reply(sender_mac, sender_ip);
    }
}

static void handle_dns_payload(const uint8_t *payload, uint16_t len, uint16_t src_port, uint16_t dst_port) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t off = 12;

    if (!g_dns_waiting) return;
    if (src_port != DNS_PORT) return;
    if (dst_port != g_dns_expected_src_port) return;
    if (len < 12) {
        g_dns_waiting = false;
        return;
    }

    id = rd16be(&payload[0]);
    if (id != g_dns_expected_id) {
        return;
    }

    flags = rd16be(&payload[2]);
    if ((flags & 0x8000u) == 0) {
        return;
    }

    qdcount = rd16be(&payload[4]);
    ancount = rd16be(&payload[6]);

    g_dns_reply_has_a = false;
    memset(g_dns_reply_ip, 0, sizeof(g_dns_reply_ip));

    for (uint16_t i = 0; i < qdcount; i++) {
        if (!dns_skip_name(payload, len, &off)) {
            g_dns_reply = true;
            g_dns_waiting = false;
            return;
        }
        if ((uint16_t)(off + 4u) > len) {
            g_dns_reply = true;
            g_dns_waiting = false;
            return;
        }
        off = (uint16_t)(off + 4u);
    }

    if ((flags & 0x000Fu) == 0) {
        for (uint16_t i = 0; i < ancount; i++) {
            uint16_t type;
            uint16_t class_code;
            uint16_t rdlen;

            if (!dns_skip_name(payload, len, &off)) break;
            if ((uint16_t)(off + 10u) > len) break;

            type = rd16be(&payload[off]);
            class_code = rd16be(&payload[off + 2]);
            rdlen = rd16be(&payload[off + 8]);
            off = (uint16_t)(off + 10u);

            if ((uint16_t)(off + rdlen) > len) break;

            if (type == 1u && class_code == 1u && rdlen == 4u) {
                memcpy(g_dns_reply_ip, &payload[off], 4);
                g_dns_reply_has_a = true;
                break;
            }

            off = (uint16_t)(off + rdlen);
        }
    }

    g_dns_reply = true;
    g_dns_waiting = false;
}

static void handle_udp_packet(const uint8_t *packet, uint16_t len) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t udp_len;

    if (len < 8) return;

    src_port = rd16be(&packet[0]);
    dst_port = rd16be(&packet[2]);
    udp_len = rd16be(&packet[4]);

    if (udp_len < 8 || udp_len > len) {
        return;
    }

    handle_dns_payload(&packet[8], (uint16_t)(udp_len - 8u), src_port, dst_port);
}

static void handle_ipv4_packet(const uint8_t *eth_src, const uint8_t *packet, uint16_t len) {
    if (len < 20) return;

    uint8_t version = (uint8_t)(packet[0] >> 4);
    uint8_t ihl = (uint8_t)((packet[0] & 0x0Fu) * 4u);
    if (version != 4 || ihl < 20 || len < ihl) return;

    uint16_t total_len = rd16be(&packet[2]);
    if (total_len < ihl) return;
    if (total_len > len) total_len = len;

    if ((rd16be(&packet[6]) & 0x1FFFu) != 0) {
        return;
    }

    const uint8_t *src_ip = &packet[12];
    const uint8_t *dst_ip = &packet[16];

    if (!bytes_equal(dst_ip, g_ip, 4)) {
        return;
    }

    if (packet[9] == IPV4_PROTO_UDP) {
        handle_udp_packet(&packet[ihl], (uint16_t)(total_len - ihl));
        return;
    }

    if (packet[9] != IPV4_PROTO_ICMP) {
        return;
    }

    const uint8_t *icmp = &packet[ihl];
    uint16_t icmp_len = (uint16_t)(total_len - ihl);
    if (icmp_len < 8) return;

    uint8_t icmp_type = icmp[0];
    uint16_t ident = rd16be(&icmp[4]);
    uint16_t seq = rd16be(&icmp[6]);

    if (icmp_type == 8) {
        const uint8_t *payload = &icmp[8];
        uint16_t payload_len = (uint16_t)(icmp_len - 8u);
        (void)send_icmp_packet(eth_src, src_ip, 0, ident, seq, payload, payload_len);
        return;
    }

    if (icmp_type == 0 && g_ping_waiting) {
        if (ident == g_ping_id && seq == g_ping_expected_seq &&
            bytes_equal(src_ip, g_ping_target, 4)) {
            g_ping_reply_ttl = packet[8];
            g_ping_reply_bytes = (uint16_t)(icmp_len - 8u);
            g_ping_reply = true;
            g_ping_waiting = false;
        }
    }
}

static void handle_ethernet_frame(const uint8_t *frame, uint16_t len) {
    if (len < 14) return;

    const uint8_t *dst_mac = &frame[0];
    const uint8_t *src_mac = &frame[6];
    uint16_t eth_type = rd16be(&frame[12]);

    if (!bytes_equal(dst_mac, g_mac, 6) && !is_broadcast_mac(dst_mac)) {
        return;
    }

    const uint8_t *payload = &frame[14];
    uint16_t payload_len = (uint16_t)(len - 14u);

    if (eth_type == ETH_TYPE_ARP) {
        handle_arp_packet(payload, payload_len);
    } else if (eth_type == ETH_TYPE_IPV4) {
        handle_ipv4_packet(src_mac, payload, payload_len);
    }
}

static void ne_receive_packets(void) {
    for (;;) {
        uint8_t curr = ne_curr_page();
        uint8_t bnry = ne_read(NE_BNRY);

        uint8_t page = (uint8_t)(bnry + 1u);
        if (page >= NE_RX_STOP_PAGE) page = NE_RX_START_PAGE;

        if (page == curr) {
            break;
        }

        uint8_t hdr[4];
        if (!ne_dma_read((uint16_t)((uint16_t)page << 8), hdr, 4)) {
            break;
        }

        uint8_t status = hdr[0];
        uint8_t next_page = hdr[1];
        uint16_t count = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);

        if (next_page < NE_RX_START_PAGE || next_page >= NE_RX_STOP_PAGE) {
            next_page = curr;
        }
        if (next_page == page) {
            next_page = (uint8_t)(page + 1u);
            if (next_page >= NE_RX_STOP_PAGE) next_page = NE_RX_START_PAGE;
        }

        if ((status & 0x01u) && count >= 4u) {
            uint16_t frame_len = (uint16_t)(count - 4u);
            if (frame_len > (uint16_t)sizeof(g_rx_buf)) {
                frame_len = (uint16_t)sizeof(g_rx_buf);
            }

            uint16_t start_addr = (uint16_t)(((uint16_t)page << 8) + 4u);
            uint16_t ring_end = (uint16_t)((uint16_t)NE_RX_STOP_PAGE << 8);

            if ((uint32_t)start_addr + frame_len <= ring_end) {
                (void)ne_dma_read(start_addr, g_rx_buf, frame_len);
            } else {
                uint16_t first = (uint16_t)(ring_end - start_addr);
                uint16_t second = (uint16_t)(frame_len - first);
                (void)ne_dma_read(start_addr, g_rx_buf, first);
                (void)ne_dma_read((uint16_t)((uint16_t)NE_RX_START_PAGE << 8), &g_rx_buf[first], second);
            }

            handle_ethernet_frame(g_rx_buf, frame_len);
        }

        uint8_t new_bnry = (uint8_t)(next_page - 1u);
        if (new_bnry < NE_RX_START_PAGE) {
            new_bnry = (uint8_t)(NE_RX_STOP_PAGE - 1u);
        }
        ne_write(NE_BNRY, new_bnry);
    }
}

static bool ne_hw_init(void) {
    if (!g_present || g_io_base == 0) {
        return false;
    }

    uint8_t reset_val = inb(ne_port(NE_RESET));
    outb(ne_port(NE_RESET), reset_val);

    if (!ne_wait_isr(NE_ISR_RST, 200000u)) {
        return false;
    }

    ne_write(NE_ISR, 0xFF);
    ne_write(NE_CR, (uint8_t)(NE_CR_STP | NE_CR_RD2 | NE_CR_PS0));

    /* 16-bit transfers, 8-byte FIFO threshold. */
    ne_write(NE_DCR, 0x49);
    ne_write(NE_RBCR0, 0x00);
    ne_write(NE_RBCR1, 0x00);
    ne_write(NE_RCR, 0x20);  /* monitor mode while configuring */
    ne_write(NE_TCR, 0x02);  /* loopback while configuring */

    ne_write(NE_TPSR, NE_TX_START_PAGE);
    ne_write(NE_PSTART, NE_RX_START_PAGE);
    ne_write(NE_BNRY, NE_RX_START_PAGE);
    ne_write(NE_PSTOP, NE_RX_STOP_PAGE);
    ne_write(NE_ISR, 0xFF);
    ne_write(NE_IMR, 0x00);

    /* Read station PROM (first 6 bytes are MAC in low byte of each word). */
    uint8_t prom[32];
    if (!ne_dma_read(0x0000, prom, (uint16_t)sizeof(prom))) {
        return false;
    }

    for (int i = 0; i < 6; i++) {
        g_mac[i] = prom[i * 2];
    }

    bool all_zero = true;
    for (int i = 0; i < 6; i++) {
        if (g_mac[i] != 0) {
            all_zero = false;
            break;
        }
    }
    if (all_zero) {
        for (int i = 0; i < 6; i++) {
            g_mac[i] = prom[i];
        }
    }

    ne_write(NE_CR, (uint8_t)(NE_CR_STP | NE_CR_RD2 | NE_CR_PS1));
    for (int i = 0; i < 6; i++) {
        ne_write((uint8_t)(NE_P1_PAR0 + i), g_mac[i]);
    }
    for (int i = 0; i < 8; i++) {
        ne_write((uint8_t)(NE_P1_MAR0 + i), 0xFF);
    }
    ne_write(NE_P1_CURR, (uint8_t)(NE_RX_START_PAGE + 1));

    ne_write(NE_CR, (uint8_t)(NE_CR_STA | NE_CR_RD2 | NE_CR_PS0));
    ne_write(NE_BNRY, NE_RX_START_PAGE);
    ne_write(NE_TCR, 0x00);  /* normal transmit */
    ne_write(NE_RCR, 0x04);  /* accept broadcast */
    ne_write(NE_ISR, 0xFF);

    g_ready = true;
    g_arp_valid = false;
    g_ping_waiting = false;
    g_ping_reply = false;
    g_ping_reply_ttl = 0;
    g_ping_reply_bytes = 0;
    g_dns_waiting = false;
    g_dns_reply = false;
    g_dns_reply_has_a = false;
    memset(g_dns_reply_ip, 0, sizeof(g_dns_reply_ip));
    g_dns_expected_id = 0;
    g_dns_expected_src_port = 0;

    return true;
}

static bool arp_resolve(const uint8_t ip[4], uint8_t out_mac[6], uint32_t wait_loops) {
    if (g_arp_valid && bytes_equal(g_arp_ip, ip, 4)) {
        memcpy(out_mac, g_arp_mac, 6);
        return true;
    }

    for (int attempt = 0; attempt < 3; attempt++) {
        send_arp_request(ip);

        for (uint32_t i = 0; i < wait_loops; i++) {
            network_poll();
            if (g_arp_valid && bytes_equal(g_arp_ip, ip, 4)) {
                memcpy(out_mac, g_arp_mac, 6);
                return true;
            }
        }
    }

    return false;
}

bool network_init(void) {
    if (g_ready) return true;

    if (!g_probe_done) {
        g_probe_done = true;
        (void)probe_ne2000_pci();
    }

    if (!g_present) return false;

    return ne_hw_init();
}

void network_poll(void) {
    if (!g_ready) return;

    uint8_t isr = ne_read(NE_ISR);
    if (isr & NE_ISR_OVW) {
        ne_write(NE_CR, (uint8_t)(NE_CR_STP | NE_CR_RD2 | NE_CR_PS0));
        ne_write(NE_BNRY, NE_RX_START_PAGE);
        ne_write(NE_CR, (uint8_t)(NE_CR_STA | NE_CR_RD2 | NE_CR_PS0));
    }

    ne_receive_packets();
    ne_write(NE_ISR, 0xFF);
}

void network_get_info(net_info_t *out) {
    if (!out) return;

    if (!g_probe_done) {
        g_probe_done = true;
        (void)probe_ne2000_pci();
    }

    out->present = g_present;
    out->ready = g_ready;
    out->pci_slot = g_pci_slot;
    out->vendor_id = g_vendor_id;
    out->device_id = g_device_id;
    out->io_base = g_io_base;
    memcpy(out->mac, g_mac, 6);
    memcpy(out->ip, g_ip, 4);
    memcpy(out->gateway, g_gateway, 4);
    memcpy(out->netmask, g_netmask, 4);
}

bool network_parse_ipv4(const char *text, uint8_t out_ip[4]) {
    if (!text || !out_ip) return false;

    const char *p = text;
    while (*p == ' ') p++;

    for (int part = 0; part < 4; part++) {
        if (*p < '0' || *p > '9') {
            return false;
        }

        int value = 0;
        while (*p >= '0' && *p <= '9') {
            value = value * 10 + (*p - '0');
            if (value > 255) {
                return false;
            }
            p++;
        }

        out_ip[part] = (uint8_t)value;

        if (part < 3) {
            if (*p != '.') {
                return false;
            }
            p++;
        }
    }

    while (*p == ' ') p++;
    return *p == '\0';
}

bool network_dns_resolve_a(const char *host, uint32_t timeout_loops, uint8_t out_ip[4], net_dns_result_t *result) {
    char host_buf[128];
    uint16_t host_len = 0;
    uint8_t next_hop[4];
    uint8_t dst_mac[6];
    uint8_t query[DNS_MAX_PACKET];
    uint16_t query_len = 0;
    uint16_t dns_id;
    uint16_t src_port;

    if (result) {
        result->success = false;
        result->ip[0] = result->ip[1] = result->ip[2] = result->ip[3] = 0;
        result->time_ms = 0;
    }

    if (!host || !out_ip) {
        return false;
    }

    while (*host == ' ') host++;
    while (*host && *host != ' ' && host_len < (uint16_t)(sizeof(host_buf) - 1u)) {
        host_buf[host_len++] = *host++;
    }
    host_buf[host_len] = '\0';

    if (host_len == 0) {
        return false;
    }

    if (!network_init()) {
        return false;
    }

    if (strcmp(host_buf, "localhost") == 0) {
        memcpy(out_ip, g_ip, 4);
        if (result) {
            result->success = true;
            memcpy(result->ip, out_ip, 4);
            result->time_ms = 0;
        }
        return true;
    }

    if (network_parse_ipv4(host_buf, out_ip)) {
        if (result) {
            result->success = true;
            memcpy(result->ip, out_ip, 4);
            result->time_ms = 0;
        }
        return true;
    }

    route_next_hop(g_dns_server, next_hop);
    if (!arp_resolve(next_hop, dst_mac, 120000u)) {
        return false;
    }

    dns_id = g_dns_next_id++;
    if (dns_id == 0) {
        dns_id = g_dns_next_id++;
    }

    if (g_dns_src_port < DNS_SRC_PORT_MIN || g_dns_src_port > DNS_SRC_PORT_MAX) {
        g_dns_src_port = DNS_SRC_PORT_MIN;
    }
    src_port = g_dns_src_port++;
    if (g_dns_src_port > DNS_SRC_PORT_MAX) {
        g_dns_src_port = DNS_SRC_PORT_MIN;
    }

    if (!build_dns_query(host_buf, dns_id, query, (uint16_t)sizeof(query), &query_len)) {
        return false;
    }

    g_dns_waiting = true;
    g_dns_reply = false;
    g_dns_reply_has_a = false;
    memset(g_dns_reply_ip, 0, sizeof(g_dns_reply_ip));
    g_dns_expected_id = dns_id;
    g_dns_expected_src_port = src_port;

    if (!send_udp_packet(dst_mac, g_dns_server, src_port, DNS_PORT, query, query_len)) {
        g_dns_waiting = false;
        return false;
    }

    for (uint32_t i = 0; i < timeout_loops; i++) {
        network_poll();

        if (!g_dns_waiting) {
            if (g_dns_reply && g_dns_reply_has_a) {
                memcpy(out_ip, g_dns_reply_ip, 4);
                if (result) {
                    result->success = true;
                    memcpy(result->ip, out_ip, 4);
                    result->time_ms = (i / PING_LOOPS_PER_MS) + 1u;
                }
                return true;
            }
            return false;
        }
    }

    g_dns_waiting = false;
    return false;
}

bool network_ping_ipv4(const uint8_t dst_ip[4], uint32_t timeout_loops, net_ping_result_t *result) {
    if (result) {
        result->success = false;
        result->bytes = 0;
        result->ttl = 0;
        result->time_ms = 0;
    }

    if (!dst_ip) return false;

    if (!network_init()) {
        return false;
    }

    if (bytes_equal(dst_ip, g_ip, 4)) {
        if (result) {
            result->success = true;
            result->bytes = 32;
            result->ttl = 64;
            result->time_ms = 0;
        }
        return true;
    }

    uint8_t next_hop[4];
    route_next_hop(dst_ip, next_hop);

    uint8_t dst_mac[6];
    if (!arp_resolve(next_hop, dst_mac, 120000u)) {
        return false;
    }

    uint8_t payload[32];
    for (int i = 0; i < 32; i++) {
        payload[i] = (uint8_t)('A' + (i % 26));
    }

    uint16_t seq = g_ping_seq++;
    g_ping_waiting = true;
    g_ping_reply = false;
    g_ping_reply_ttl = 0;
    g_ping_reply_bytes = 0;
    g_ping_expected_seq = seq;
    memcpy(g_ping_target, dst_ip, 4);

    if (!send_icmp_packet(dst_mac, dst_ip, 8, g_ping_id, seq, payload, (uint16_t)sizeof(payload))) {
        g_ping_waiting = false;
        return false;
    }

    for (uint32_t i = 0; i < timeout_loops; i++) {
        network_poll();
        if (g_ping_reply) {
            if (result) {
                uint32_t ms = (i / PING_LOOPS_PER_MS) + 1u;
                result->success = true;
                result->bytes = g_ping_reply_bytes;
                result->ttl = g_ping_reply_ttl;
                result->time_ms = ms;
            }
            return true;
        }
    }

    g_ping_waiting = false;
    return false;
}
