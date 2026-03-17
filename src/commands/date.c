#include "terminal.h"
#include "io.h"
#include "string.h"

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t v) {
    return (uint8_t)(((v >> 4) * 10) + (v & 0x0F));
}

static bool rtc_wait_ready(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if ((cmos_read(0x0A) & 0x80u) == 0) {
            return true;
        }
    }
    return false;
}

static void print2(uint8_t v) {
    char s[3];
    s[0] = (char)('0' + (v / 10));
    s[1] = (char)('0' + (v % 10));
    s[2] = '\0';
    term_puts(s);
}

void cmd_date(const char *args) {
    (void)args;

    if (!rtc_wait_ready()) {
        term_puts("date: RTC busy\n");
        return;
    }

    uint8_t sec   = cmos_read(0x00);
    uint8_t min   = cmos_read(0x02);
    uint8_t hour  = cmos_read(0x04);
    uint8_t day   = cmos_read(0x07);
    uint8_t month = cmos_read(0x08);
    uint8_t year  = cmos_read(0x09);
    uint8_t reg_b = cmos_read(0x0B);

    if ((reg_b & 0x04u) == 0) {
        sec   = bcd_to_bin(sec);
        min   = bcd_to_bin(min);
        hour  = bcd_to_bin((uint8_t)(hour & 0x7Fu));
        day   = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year  = bcd_to_bin(year);
    }

    if ((reg_b & 0x02u) == 0) {
        bool pm = (hour & 0x80u) != 0;
        hour &= 0x7Fu;
        if (pm && hour < 12) hour = (uint8_t)(hour + 12);
        if (!pm && hour == 12) hour = 0;
    }

    char ybuf[8];
    itoa((int)(2000 + year), ybuf, 10);
    term_puts(ybuf);
    term_puts("-");
    print2(month);
    term_puts("-");
    print2(day);
    term_puts(" ");
    print2(hour);
    term_puts(":");
    print2(min);
    term_puts(":");
    print2(sec);
    term_puts("\n");
}
