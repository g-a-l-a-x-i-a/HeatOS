#ifndef NETWORK_H
#define NETWORK_H

#include "types.h"

typedef struct {
    bool     present;
    bool     ready;
    uint8_t  pci_slot;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t io_base;
    uint8_t  mac[6];
    uint8_t  ip[4];
    uint8_t  gateway[4];
    uint8_t  netmask[4];
} net_info_t;

typedef struct {
    bool     success;
    uint16_t bytes;
    uint8_t  ttl;
    uint32_t time_ms;
} net_ping_result_t;

typedef struct {
    bool     success;
    uint8_t  ip[4];
    uint32_t time_ms;
} net_dns_result_t;

bool network_init(void);
void network_poll(void);
void network_get_info(net_info_t *out);

bool network_parse_ipv4(const char *text, uint8_t out_ip[4]);
bool network_dns_resolve_a(const char *host, uint32_t timeout_loops, uint8_t out_ip[4], net_dns_result_t *result);
bool network_ping_ipv4(const uint8_t dst_ip[4], uint32_t timeout_loops, net_ping_result_t *result);

#endif
