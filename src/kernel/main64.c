#include "types.h"
#include "io.h"
#include "serial.h"
#include "ramdisk.h"
#include "string.h"
#include "keyboard.h"

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define TERM_ATTR   0x07

#define KB_DATA_PORT   0x60
#define KB_STATUS_PORT 0x64
#define KB_STATUS_OBF  0x01

typedef struct {
    char vendor[13];
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    bool has_apic;
    bool has_sse2;
    bool has_long_mode;
} cpu_info64_t;

static volatile uint16_t *const g_vga = (volatile uint16_t *)0xB8000;
static int g_row = 0;
static int g_col = 0;
static cpu_info64_t g_cpu;

/* US QWERTY scancode set 1 -> ASCII (lowercase) */
static const char scancode_ascii[] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', 8,
    9,  'q','w','e','r','t','y','u','i','o','p','[',']', 13, 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ', 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0
};

/* Shifted characters */
static const char scancode_shift[] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', 8,
    9,  'Q','W','E','R','T','Y','U','I','O','P','{','}', 13, 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0, '|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static bool g_shift_left = false;
static bool g_shift_right = false;
static bool g_extended = false;

static uint16_t rd_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t rd_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void cpuid_ex(uint32_t leaf, uint32_t subleaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile(
        "cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf), "c"(subleaf)
    );
}

static void str_to_lower_in_place(char *s) {
    if (!s) return;
    while (*s) {
        if (*s >= 'A' && *s <= 'Z') {
            *s = (char)(*s - 'A' + 'a');
        }
        s++;
    }
}

static void vga_sync_cursor(void) {
    uint16_t pos = (uint16_t)(g_row * VGA_WIDTH + g_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_scroll_up(void) {
    for (int r = 1; r < VGA_HEIGHT; r++) {
        for (int c = 0; c < VGA_WIDTH; c++) {
            g_vga[(r - 1) * VGA_WIDTH + c] = g_vga[r * VGA_WIDTH + c];
        }
    }
    for (int c = 0; c < VGA_WIDTH; c++) {
        g_vga[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = (uint16_t)TERM_ATTR << 8 | ' ';
    }
    g_row = VGA_HEIGHT - 1;
    g_col = 0;
}

static void term_putc(char ch) {
    if (ch == '\n') {
        serial_putc('\n');
        g_col = 0;
        g_row++;
        if (g_row >= VGA_HEIGHT) {
            vga_scroll_up();
        }
        vga_sync_cursor();
        return;
    }

    if (ch == '\b') {
        if (g_col > 0) {
            g_col--;
            g_vga[g_row * VGA_WIDTH + g_col] = (uint16_t)TERM_ATTR << 8 | ' ';
            vga_sync_cursor();
        }
        return;
    }

    serial_putc(ch);
    g_vga[g_row * VGA_WIDTH + g_col] = (uint16_t)TERM_ATTR << 8 | (uint8_t)ch;
    g_col++;
    if (g_col >= VGA_WIDTH) {
        g_col = 0;
        g_row++;
        if (g_row >= VGA_HEIGHT) {
            vga_scroll_up();
        }
    }
    vga_sync_cursor();
}

static void term_puts(const char *s) {
    while (s && *s) {
        term_putc(*s++);
    }
}

static void term_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        g_vga[i] = (uint16_t)TERM_ATTR << 8 | ' ';
    }
    g_row = 0;
    g_col = 0;
    vga_sync_cursor();
}

static void term_print_u32(uint32_t v, int base) {
    char buf[16];
    utoa(v, buf, base);
    term_puts(buf);
}

static void term_print_i32(int32_t v) {
    char buf[16];
    itoa((int)v, buf, 10);
    term_puts(buf);
}

static bool shift_held(void) {
    return g_shift_left || g_shift_right;
}

static int keyboard_poll_ps2(void) {
    uint8_t status = inb(KB_STATUS_PORT);
    if ((status & KB_STATUS_OBF) == 0) {
        return 0;
    }

    uint8_t sc = inb(KB_DATA_PORT);

    if (sc == 0xE0) {
        g_extended = true;
        return 0;
    }

    bool released = (sc & 0x80u) != 0u;
    uint8_t code = (uint8_t)(sc & 0x7Fu);

    if (code == 0x2A) {
        g_shift_left = !released;
        return 0;
    }

    if (code == 0x36) {
        g_shift_right = !released;
        return 0;
    }

    if (released) {
        g_extended = false;
        return 0;
    }

    if (g_extended) {
        g_extended = false;
        switch (code) {
            case 0x48: return KEY_UP;
            case 0x50: return KEY_DOWN;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
            case 0x47: return KEY_HOME;
            case 0x4F: return KEY_END;
            case 0x53: return KEY_DELETE;
            default: return 0;
        }
    }

    switch (code) {
        case 0x0E: return KEY_BACKSPACE;
        case 0x1C: return KEY_ENTER;
        case 0x01: return KEY_ESCAPE;
        default: break;
    }

    if (code < 128) {
        char c = shift_held() ? scancode_shift[code] : scancode_ascii[code];
        if (c) {
            return (int)(uint8_t)c;
        }
    }

    return 0;
}

static void cpu_collect(cpu_info64_t *out) {
    uint32_t a, b, c, d;
    uint32_t max_leaf;
    uint32_t max_ext_leaf;

    memset(out, 0, sizeof(*out));

    cpuid_ex(0, 0, &a, &b, &c, &d);
    max_leaf = a;

    out->vendor[0] = (char)(b & 0xFF);
    out->vendor[1] = (char)((b >> 8) & 0xFF);
    out->vendor[2] = (char)((b >> 16) & 0xFF);
    out->vendor[3] = (char)((b >> 24) & 0xFF);
    out->vendor[4] = (char)(d & 0xFF);
    out->vendor[5] = (char)((d >> 8) & 0xFF);
    out->vendor[6] = (char)((d >> 16) & 0xFF);
    out->vendor[7] = (char)((d >> 24) & 0xFF);
    out->vendor[8] = (char)(c & 0xFF);
    out->vendor[9] = (char)((c >> 8) & 0xFF);
    out->vendor[10] = (char)((c >> 16) & 0xFF);
    out->vendor[11] = (char)((c >> 24) & 0xFF);
    out->vendor[12] = '\0';

    if (max_leaf >= 1) {
        uint32_t ext_family;
        uint32_t ext_model;
        uint32_t family;
        uint32_t model;

        cpuid_ex(1, 0, &a, &b, &c, &d);
        out->stepping = a & 0x0Fu;
        model = (a >> 4) & 0x0Fu;
        family = (a >> 8) & 0x0Fu;
        ext_model = (a >> 16) & 0x0Fu;
        ext_family = (a >> 20) & 0xFFu;

        if (family == 0x06u || family == 0x0Fu) {
            model |= (ext_model << 4);
        }
        if (family == 0x0Fu) {
            family += ext_family;
        }

        out->family = family;
        out->model = model;
        out->has_apic = ((d & (1u << 9)) != 0u);
        out->has_sse2 = ((d & (1u << 26)) != 0u);
    }

    cpuid_ex(0x80000000u, 0, &a, &b, &c, &d);
    max_ext_leaf = a;
    if (max_ext_leaf >= 0x80000001u) {
        cpuid_ex(0x80000001u, 0, &a, &b, &c, &d);
        out->has_long_mode = ((d & (1u << 29)) != 0u);
    }
}

static void cmd_info(void) {
    term_puts("CPU vendor: ");
    term_puts(g_cpu.vendor);
    term_putc('\n');

    term_puts("Family/Model/Stepping: ");
    term_print_u32(g_cpu.family, 10);
    term_puts("/");
    term_print_u32(g_cpu.model, 10);
    term_puts("/");
    term_print_u32(g_cpu.stepping, 10);
    term_putc('\n');

    term_puts("Features: APIC=");
    term_puts(g_cpu.has_apic ? "yes" : "no");
    term_puts(" SSE2=");
    term_puts(g_cpu.has_sse2 ? "yes" : "no");
    term_puts(" LongMode=");
    term_puts(g_cpu.has_long_mode ? "yes" : "no");
    term_putc('\n');
}

static bool resolve_node(const char *path, fs_node_t *out) {
    if (!out) return false;
    if (!path || !*path) {
        *out = fs_cwd_get();
        return true;
    }
    return fs_resolve_checked(path, out);
}

static void cmd_pwd(void) {
    char path[64];
    fs_build_path(fs_cwd_get(), path, sizeof(path));
    term_puts(path);
    term_putc('\n');
}

static void cmd_cd(const char *args) {
    fs_node_t node;
    if (!args || !*args) {
        fs_cwd_set(0);
        return;
    }

    if (!resolve_node(args, &node) || !fs_is_dir(node)) {
        term_puts("cd: directory not found\n");
        return;
    }

    fs_cwd_set(node);
}

static void cmd_ls(const char *args) {
    fs_node_t node;
    fs_node_t iter;
    fs_node_t child;

    if (!resolve_node(args, &node)) {
        term_puts("ls: path not found\n");
        return;
    }

    if (fs_is_file(node)) {
        term_puts(fs_get_name(node));
        term_putc('\n');
        return;
    }

    if (!fs_list_begin(node, &iter)) {
        term_puts("(empty)\n");
        return;
    }

    while (fs_list_next(&iter, &child)) {
        term_puts(fs_is_dir(child) ? "[D] " : "[F] ");
        term_puts(fs_get_name(child));
        term_putc('\n');
    }
}

static void cmd_cat(const char *args) {
    fs_node_t node;
    char buf[1024];
    int n;

    if (!args || !*args) {
        term_puts("cat: missing file path\n");
        return;
    }

    if (!resolve_node(args, &node) || !fs_is_file(node)) {
        term_puts("cat: file not found\n");
        return;
    }

    n = fs_read(node, buf, (int)sizeof(buf) - 1);
    if (n < 0) {
        term_puts("cat: read failed\n");
        return;
    }

    buf[n] = '\0';
    term_puts(buf);
    if (n > 0 && buf[n - 1] != '\n') {
        term_putc('\n');
    }
}

static int java_detect_format(const uint8_t *data, int size) {
    if (!data || size < 4) return 0;
    if (size >= 5 && data[0] == 'H' && data[1] == 'J' && data[2] == 'A' && data[3] == 'R') return 1;
    if (data[0] == 'P' && data[1] == 'K' && data[2] == 0x03 && data[3] == 0x04) return 2;
    if (rd_be32(data) == 0xCAFEBABEu) return 3;
    return 0;
}

static bool java_hjar_exec(const uint8_t *data, int size) {
    int pc = 5;
    int32_t stack[64];
    int sp = 0;

    if (size < 5 || java_detect_format(data, size) != 1) {
        term_puts("java: invalid HJAR\n");
        return false;
    }

    while (pc < size) {
        uint8_t op = data[pc++];

        if (op == 0x10) {
            if (pc >= size || sp >= 64) {
                term_puts("java: HJAR stack/bytecode error\n");
                return false;
            }
            stack[sp++] = (int8_t)data[pc++];
            continue;
        }

        if (op == 0x60 || op == 0x64 || op == 0x68 || op == 0x6C) {
            int32_t b;
            int32_t a;
            if (sp < 2) {
                term_puts("java: HJAR stack underflow\n");
                return false;
            }
            b = stack[--sp];
            a = stack[--sp];
            if (op == 0x60) stack[sp++] = a + b;
            if (op == 0x64) stack[sp++] = a - b;
            if (op == 0x68) stack[sp++] = a * b;
            if (op == 0x6C) {
                if (b == 0) {
                    term_puts("java: divide by zero\n");
                    return false;
                }
                stack[sp++] = a / b;
            }
            continue;
        }

        if (op == 0xFC) {
            uint8_t n;
            if (pc >= size) {
                term_puts("java: HJAR truncated literal\n");
                return false;
            }
            n = data[pc++];
            if (pc + n > size) {
                term_puts("java: HJAR literal out of range\n");
                return false;
            }
            for (uint8_t i = 0; i < n; i++) {
                term_putc((char)data[pc + i]);
            }
            pc += n;
            continue;
        }

        if (op == 0xFE) {
            if (sp < 1) {
                term_puts("java: HJAR print stack underflow\n");
                return false;
            }
            term_print_i32(stack[--sp]);
            continue;
        }

        if (op == 0xB1) {
            term_putc('\n');
            return true;
        }

        term_puts("java: unknown HJAR opcode 0x");
        term_print_u32(op, 16);
        term_putc('\n');
        return false;
    }

    term_puts("java: HJAR ended without return\n");
    return false;
}

static bool java_zip_list_entries(const uint8_t *data, int size) {
    int off = 0;
    int entries = 0;

    while (off + 30 <= size) {
        uint32_t sig = rd_le32(data + off);
        uint16_t flags;
        uint16_t method;
        uint32_t comp_size;
        uint16_t name_len;
        uint16_t extra_len;
        int name_off;
        int data_off;
        char name[64];

        if (sig != 0x04034B50u) {
            break;
        }

        flags = rd_le16(data + off + 6);
        method = rd_le16(data + off + 8);
        comp_size = rd_le32(data + off + 18);
        name_len = rd_le16(data + off + 26);
        extra_len = rd_le16(data + off + 28);

        name_off = off + 30;
        data_off = name_off + name_len + extra_len;

        if (name_off + name_len > size || data_off > size) {
            term_puts("jarinfo: truncated header\n");
            return false;
        }

        if ((flags & 0x0008u) != 0u) {
            term_puts("jarinfo: data-descriptor jars not supported yet\n");
            return false;
        }

        if (data_off + (int)comp_size > size) {
            term_puts("jarinfo: entry size out of range\n");
            return false;
        }

        memset(name, 0, sizeof(name));
        memcpy(name, data + name_off, name_len < (sizeof(name) - 1) ? name_len : (sizeof(name) - 1));

        term_puts("  - ");
        term_puts(name);
        term_puts("  method=");
        term_print_u32(method, 10);
        term_puts("  bytes=");
        term_print_u32(comp_size, 10);
        term_putc('\n');

        off = data_off + (int)comp_size;
        entries++;
    }

    if (entries == 0) {
        term_puts("jarinfo: no local file headers found\n");
        return false;
    }

    term_puts("jarinfo: entries parsed=");
    term_print_u32((uint32_t)entries, 10);
    term_putc('\n');
    return true;
}

static void java_class_info(const uint8_t *data, int size) {
    uint16_t minor;
    uint16_t major;
    uint16_t cp_count;

    if (!data || size < 10 || rd_be32(data) != 0xCAFEBABEu) {
        term_puts("java: invalid class file\n");
        return;
    }

    minor = rd_be16(data + 4);
    major = rd_be16(data + 6);
    cp_count = rd_be16(data + 8);

    term_puts("ClassFile version: ");
    term_print_u32(major, 10);
    term_putc('.');
    term_print_u32(minor, 10);
    term_puts("  cp_count=");
    term_print_u32(cp_count, 10);
    term_putc('\n');
    term_puts("Execution: full JVM class execution is not implemented yet.\n");
}

static void load_file_to_buf(const char *path, uint8_t *buf, int cap, int *out_n, int *out_fmt) {
    fs_node_t node;
    int n;
    int fmt;

    *out_n = -1;
    *out_fmt = 0;

    if (!path || !*path) {
        term_puts("java: missing file path\n");
        return;
    }

    if (!resolve_node(path, &node) || !fs_is_file(node)) {
        term_puts("java: file not found\n");
        return;
    }

    n = fs_read(node, buf, cap);
    if (n <= 0) {
        term_puts("java: failed to read file\n");
        return;
    }

    fmt = java_detect_format(buf, n);
    *out_n = n;
    *out_fmt = fmt;
}

static void cmd_jarinfo(const char *args) {
    uint8_t buf[4096];
    int n;
    int fmt;

    load_file_to_buf(args, buf, (int)sizeof(buf), &n, &fmt);
    if (n <= 0) return;

    if (fmt == 1) {
        term_puts("jarinfo: HJAR (HeatOS mini format), version=");
        term_print_u32(buf[4], 10);
        term_puts(" size=");
        term_print_u32((uint32_t)n, 10);
        term_putc('\n');
        return;
    }

    if (fmt == 2) {
        term_puts("jarinfo: ZIP/JAR detected\n");
        (void)java_zip_list_entries(buf, n);
        return;
    }

    if (fmt == 3) {
        term_puts("jarinfo: ClassFile detected\n");
        java_class_info(buf, n);
        return;
    }

    term_puts("jarinfo: unknown file format\n");
}

static void cmd_java(const char *args) {
    uint8_t buf[4096];
    int n;
    int fmt;

    load_file_to_buf(args, buf, (int)sizeof(buf), &n, &fmt);
    if (n <= 0) return;

    if (fmt == 1) {
        term_puts("java: running HJAR app\n");
        (void)java_hjar_exec(buf, n);
        return;
    }

    if (fmt == 2) {
        term_puts("java: ZIP/JAR detected. Entry listing:\n");
        (void)java_zip_list_entries(buf, n);
        term_puts("java: full JVM .jar execution is in progress (class loader + verifier pending).\n");
        return;
    }

    if (fmt == 3) {
        term_puts("java: direct .class input detected\n");
        java_class_info(buf, n);
        return;
    }

    term_puts("java: unsupported file format\n");
}

static void cmd_help(void) {
    term_puts("x64 monitor commands:\n");
    term_puts("  help               - show this list\n");
    term_puts("  clear              - clear screen\n");
    term_puts("  info               - CPU + feature info\n");
    term_puts("  pwd                - print working directory\n");
    term_puts("  cd <path>          - change directory\n");
    term_puts("  ls [path]          - list files\n");
    term_puts("  cat <path>         - print file\n");
    term_puts("  jarinfo <path>     - inspect HJAR/ZIP/Class file\n");
    term_puts("  java <path>        - run HJAR or inspect ZIP/Class\n");
    term_puts("  reboot             - keyboard-controller reset\n");
    term_puts("  halt               - halt CPU\n");
}

static void shell_exec_line(char *line) {
    char *args;
    if (!line || !*line) return;

    args = strchr(line, ' ');
    if (args) {
        *args = '\0';
        args++;
        while (*args == ' ') args++;
        if (*args == '\0') args = (void*)0;
    }

    str_to_lower_in_place(line);

    if (strcmp(line, "help") == 0) {
        cmd_help();
    } else if (strcmp(line, "clear") == 0) {
        term_clear();
    } else if (strcmp(line, "info") == 0) {
        cmd_info();
    } else if (strcmp(line, "pwd") == 0) {
        cmd_pwd();
    } else if (strcmp(line, "cd") == 0) {
        cmd_cd(args);
    } else if (strcmp(line, "ls") == 0) {
        cmd_ls(args);
    } else if (strcmp(line, "cat") == 0) {
        cmd_cat(args);
    } else if (strcmp(line, "jarinfo") == 0) {
        cmd_jarinfo(args);
    } else if (strcmp(line, "java") == 0) {
        cmd_java(args);
    } else if (strcmp(line, "reboot") == 0) {
        outb(0x64, 0xFE);
        for (;;) {
            __asm__ volatile("hlt");
        }
    } else if (strcmp(line, "halt") == 0) {
        term_puts("System halted.\n");
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    } else {
        term_puts("Unknown command: ");
        term_puts(line);
        term_puts("\n");
    }
}

void kernel_main64(void) {
    char line[256];
    int len = 0;

    serial_init();
    cpu_collect(&g_cpu);

    term_clear();
    term_puts("HeatOS x64 kernel (phase 2)\n");
    term_puts("Long mode active. Interactive monitor enabled.\n");
    term_puts("\nCPU: ");
    term_puts(g_cpu.vendor);
    term_puts("  LM=");
    term_puts(g_cpu.has_long_mode ? "yes" : "no");
    term_puts("\n");

    ramdisk_init();
    fs_cwd_set(0);
    term_puts("Ramdisk online (/home, /java, /docs...).\n");
    term_puts("Java phase 1: HJAR execution + ZIP/JAR inspection.\n");
    term_puts("Type 'help'.\n\n");

    term_puts("x64> ");
    line[0] = '\0';

    for (;;) {
        int key = keyboard_poll_ps2();
        if (!key) {
            __asm__ volatile("hlt");
            continue;
        }

        if (key == KEY_ENTER || key == '\n') {
            line[len] = '\0';
            term_putc('\n');
            if (len > 0) {
                shell_exec_line(line);
            }
            len = 0;
            line[0] = '\0';
            term_puts("x64> ");
            continue;
        }

        if (key == KEY_BACKSPACE || key == '\b') {
            if (len > 0) {
                len--;
                line[len] = '\0';
                term_putc('\b');
            }
            continue;
        }

        if (key >= 32 && key <= 126 && len < (int)sizeof(line) - 1) {
            line[len++] = (char)key;
            line[len] = '\0';
            term_putc((char)key);
        }
    }
}
