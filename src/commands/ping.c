#include "terminal.h"
#include "network.h"

void cmd_ping(const char *args) {
    (void)args;
    term_puts("Pinging 8.8.8.8...\n");
    uint8_t ip[4] = {8, 8, 8, 8};
    net_ping_result_t res;
    network_ping_ipv4(ip, 500000, &res);
    if (res.success) term_puts("Reply received!\n");
    else term_puts("Timeout.\n");
}
