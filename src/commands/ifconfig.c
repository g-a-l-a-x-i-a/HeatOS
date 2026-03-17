#include "terminal.h"
#include "network.h"
#include "string.h"

static char hex_digit(uint8_t v) {
    v &= 0x0Fu;
    return (char)(v < 10 ? ('0' + v) : ('A' + (v - 10)));
}

static void print_hex_byte(uint8_t b) {
    char s[3];
    s[0] = hex_digit((uint8_t)(b >> 4));
    s[1] = hex_digit(b);
    s[2] = '\0';
    term_puts(s);
}

static void print_ipv4(const uint8_t ip[4]) {
    char nbuf[16];
    itoa(ip[0], nbuf, 10); term_puts(nbuf); term_puts(".");
    itoa(ip[1], nbuf, 10); term_puts(nbuf); term_puts(".");
    itoa(ip[2], nbuf, 10); term_puts(nbuf); term_puts(".");
    itoa(ip[3], nbuf, 10); term_puts(nbuf);
}

void cmd_ifconfig(const char *args) {
    (void)args;

    net_info_t info;
    network_get_info(&info);

    if (!info.present) {
        term_puts("No network adapter found\n");
        return;
    }

    term_puts("iface0: ");
    term_puts(info.ready ? "UP\n" : "DOWN\n");

    term_puts("MAC: ");
    for (int i = 0; i < 6; i++) {
        if (i > 0) term_puts(":");
        print_hex_byte(info.mac[i]);
    }

    term_puts("\nIP: ");
    print_ipv4(info.ip);

    term_puts("\nMask: ");
    print_ipv4(info.netmask);

    term_puts("\nGateway: ");
    print_ipv4(info.gateway);

    char hbuf[16];
    term_puts("\nIO Base: 0x");
    utoa(info.io_base, hbuf, 16);
    term_puts(hbuf);

    term_puts("\nPCI Slot: ");
    itoa(info.pci_slot, hbuf, 10);
    term_puts(hbuf);

    term_puts("\n");
}
