#include "terminal.h"
#include "network.h"
#include "string.h"

void cmd_ifconfig(const char *args) {
    (void)args;
    net_info_t info;
    network_get_info(&info);
    
    if (!info.present) {
        term_puts("No network adapter found\n");
        return;
    }
    
    term_puts("Network Interface\n");
    term_puts("MAC: ");
    char buf[3];
    for (int i = 0; i < 6; i++) {
        if (i > 0) term_puts(":");
        itoa(info.mac[i], buf, 16);
        if (info.mac[i] < 16) term_puts("0");
        term_puts(buf);
    }
    term_puts("\nIP: ");
    itoa(info.ip[0], buf, 10);
    term_puts(buf);
    term_puts(".");
    itoa(info.ip[1], buf, 10);
    term_puts(buf);
    term_puts(".");
    itoa(info.ip[2], buf, 10);
    term_puts(buf);
    term_puts(".");
    itoa(info.ip[3], buf, 10);
    term_puts(buf);
    term_puts("\nReady: ");
    term_puts(info.ready ? "YES" : "NO");
    term_puts("\n");
}
