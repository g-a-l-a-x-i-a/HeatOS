#include "commands.h"
#include "terminal.h"
#include "ramdisk.h"
#include "string.h"

#define JAVA_BUF_CAP 8192

#define JVM_CP_MAX          256
#define JVM_METHOD_MAX      64
#define JVM_STACK_MAX       256
#define JVM_LOCALS_MAX      64
#define JVM_CALL_DEPTH_MAX  12

#define JVM_REF_PRINTSTREAM ((int32_t)0x7F000001)
#define JVM_REF_STRING_FLAG ((int32_t)0x40000000)
#define JVM_REF_OBJECT_FLAG ((int32_t)0x20000000)

typedef struct {
    const uint8_t *jar_data;
    int jar_size;
} jvm_runtime_t;

typedef struct {
    uint8_t tag;
    uint32_t a;
    uint32_t b;
} jcp_entry_t;

typedef struct {
    uint16_t access_flags;
    uint16_t name_index;
    uint16_t desc_index;
    uint16_t max_stack;
    uint16_t max_locals;
    const uint8_t *code;
    uint32_t code_len;
} jmethod_t;

typedef struct {
    const uint8_t *bytes;
    int size;
    uint16_t minor;
    uint16_t major;
    uint16_t cp_count;
    jcp_entry_t cp[JVM_CP_MAX];
    uint16_t this_class;
    uint16_t methods_count;
    jmethod_t methods[JVM_METHOD_MAX];
} jclass_t;

typedef struct {
    const jclass_t *cls;
    const jvm_runtime_t *rt;
    int depth;
} jvm_ctx_t;

static bool java_zip_find_entry(const uint8_t *data, int size, const char *target,
                                const uint8_t **out_payload, uint32_t *out_payload_len,
                                uint16_t *out_method);
static const char *java_default_path(void);

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

static void term_print_bytes(const char *s, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        term_putc(s[i], 0x07);
    }
}

static bool bytes_eq_lit(const char *s, uint16_t len, const char *lit) {
    size_t lit_len = strlen(lit);
    if (len != lit_len) return false;
    return memcmp(s, lit, lit_len) == 0;
}

static bool ends_with_bytes(const uint8_t *s, uint16_t len, const char *suffix) {
    size_t n = strlen(suffix);
    if ((size_t)len < n) return false;
    return memcmp(s + (len - n), suffix, n) == 0;
}

static bool cp_utf8(const jclass_t *jc, uint16_t idx, const char **out_ptr, uint16_t *out_len) {
    const jcp_entry_t *e;

    if (!jc || !out_ptr || !out_len) return false;
    if (idx == 0 || idx >= jc->cp_count) return false;

    e = &jc->cp[idx];
    if (e->tag != 1) return false;

    if ((int)e->a + (int)e->b > jc->size) return false;

    *out_ptr = (const char *)(jc->bytes + e->a);
    *out_len = (uint16_t)e->b;
    return true;
}

static bool cp_utf8_eq(const jclass_t *jc, uint16_t idx, const char *lit) {
    const char *ptr;
    uint16_t len;
    if (!cp_utf8(jc, idx, &ptr, &len)) return false;
    return bytes_eq_lit(ptr, len, lit);
}

static bool cp_class_name(const jclass_t *jc, uint16_t class_idx, const char **out_ptr, uint16_t *out_len) {
    const jcp_entry_t *e;

    if (!jc || class_idx == 0 || class_idx >= jc->cp_count) return false;
    e = &jc->cp[class_idx];
    if (e->tag != 7) return false;

    return cp_utf8(jc, (uint16_t)e->a, out_ptr, out_len);
}

static bool cp_name_type(const jclass_t *jc, uint16_t nt_idx,
                         const char **name_ptr, uint16_t *name_len,
                         const char **desc_ptr, uint16_t *desc_len) {
    const jcp_entry_t *e;
    if (!jc || nt_idx == 0 || nt_idx >= jc->cp_count) return false;
    e = &jc->cp[nt_idx];
    if (e->tag != 12) return false;

    if (!cp_utf8(jc, (uint16_t)e->a, name_ptr, name_len)) return false;
    if (!cp_utf8(jc, (uint16_t)e->b, desc_ptr, desc_len)) return false;
    return true;
}

static bool cp_resolve_ref(const jclass_t *jc, uint16_t ref_idx, uint8_t expect_tag,
                           const char **class_ptr, uint16_t *class_len,
                           const char **name_ptr, uint16_t *name_len,
                           const char **desc_ptr, uint16_t *desc_len) {
    const jcp_entry_t *e;
    if (!jc || ref_idx == 0 || ref_idx >= jc->cp_count) return false;
    e = &jc->cp[ref_idx];
    if (e->tag != expect_tag) return false;

    if (!cp_class_name(jc, (uint16_t)e->a, class_ptr, class_len)) return false;
    if (!cp_name_type(jc, (uint16_t)e->b, name_ptr, name_len, desc_ptr, desc_len)) return false;
    return true;
}

static bool parse_member_info_skip(const uint8_t *data, int size, int *io_off) {
    int off = *io_off;
    uint16_t attr_count;

    if (off + 8 > size) return false;

    attr_count = rd_be16(data + off + 6);
    off += 8;

    for (uint16_t i = 0; i < attr_count; i++) {
        uint32_t alen;
        if (off + 6 > size) return false;
        alen = rd_be32(data + off + 2);
        off += 6;
        if (off + (int)alen > size) return false;
        off += (int)alen;
    }

    *io_off = off;
    return true;
}

static bool jvm_parse_class(const uint8_t *data, int size, jclass_t *out) {
    int off;

    if (!data || !out || size < 10) return false;
    if (rd_be32(data) != 0xCAFEBABEu) return false;

    memset(out, 0, sizeof(*out));
    out->bytes = data;
    out->size = size;
    out->minor = rd_be16(data + 4);
    out->major = rd_be16(data + 6);

    off = 8;
    out->cp_count = rd_be16(data + off);
    off += 2;

    if (out->cp_count == 0 || out->cp_count > JVM_CP_MAX) {
        term_puts("java: class constant pool too large\n");
        return false;
    }

    for (uint16_t i = 1; i < out->cp_count; i++) {
        uint8_t tag;
        if (off >= size) return false;
        tag = data[off++];
        out->cp[i].tag = tag;

        switch (tag) {
            case 1: {
                uint16_t len;
                if (off + 2 > size) return false;
                len = rd_be16(data + off);
                off += 2;
                if (off + len > size) return false;
                out->cp[i].a = (uint32_t)off;
                out->cp[i].b = (uint32_t)len;
                off += len;
                break;
            }
            case 3:
            case 4:
                if (off + 4 > size) return false;
                out->cp[i].a = rd_be32(data + off);
                off += 4;
                break;
            case 5:
            case 6:
                if (off + 8 > size) return false;
                off += 8;
                i++;
                if (i < out->cp_count) out->cp[i].tag = 0;
                break;
            case 7:
            case 8:
            case 16:
                if (off + 2 > size) return false;
                out->cp[i].a = rd_be16(data + off);
                off += 2;
                break;
            case 9:
            case 10:
            case 11:
            case 12:
            case 18:
                if (off + 4 > size) return false;
                out->cp[i].a = rd_be16(data + off);
                out->cp[i].b = rd_be16(data + off + 2);
                off += 4;
                break;
            case 15:
                if (off + 3 > size) return false;
                out->cp[i].a = data[off];
                out->cp[i].b = rd_be16(data + off + 1);
                off += 3;
                break;
            default:
                term_puts("java: unsupported constant pool tag\n");
                return false;
        }
    }

    if (off + 8 > size) return false;

    out->this_class = rd_be16(data + off + 2);
    uint16_t interfaces_count = rd_be16(data + off + 6);
    off += 8;

    if (off + (int)interfaces_count * 2 > size) return false;
    off += interfaces_count * 2;

    if (off + 2 > size) return false;
    uint16_t fields_count = rd_be16(data + off);
    off += 2;
    for (uint16_t i = 0; i < fields_count; i++) {
        if (!parse_member_info_skip(data, size, &off)) return false;
    }

    if (off + 2 > size) return false;
    uint16_t methods_decl = rd_be16(data + off);
    off += 2;

    out->methods_count = 0;
    for (uint16_t i = 0; i < methods_decl; i++) {
        jmethod_t m;
        uint16_t attr_count;
        memset(&m, 0, sizeof(m));

        if (off + 8 > size) return false;
        m.access_flags = rd_be16(data + off);
        m.name_index   = rd_be16(data + off + 2);
        m.desc_index   = rd_be16(data + off + 4);
        attr_count = rd_be16(data + off + 6);
        off += 8;

        for (uint16_t a = 0; a < attr_count; a++) {
            uint16_t attr_name_idx;
            uint32_t attr_len;
            int attr_info;

            if (off + 6 > size) return false;
            attr_name_idx = rd_be16(data + off);
            attr_len = rd_be32(data + off + 2);
            off += 6;
            attr_info = off;

            if (off + (int)attr_len > size) return false;

            if (cp_utf8_eq(out, attr_name_idx, "Code")) {
                if (attr_len >= 8 && attr_info + 8 <= size) {
                    uint32_t code_len = rd_be32(data + attr_info + 4);
                    if (attr_info + 8 + (int)code_len <= off + (int)attr_len) {
                        m.max_stack  = rd_be16(data + attr_info);
                        m.max_locals = rd_be16(data + attr_info + 2);
                        m.code_len   = code_len;
                        m.code       = data + attr_info + 8;
                    }
                }
            }

            off += (int)attr_len;
        }

        if (out->methods_count < JVM_METHOD_MAX) {
            out->methods[out->methods_count++] = m;
        }
    }

    if (off + 2 > size) return false;
    uint16_t class_attrs = rd_be16(data + off);
    off += 2;
    for (uint16_t i = 0; i < class_attrs; i++) {
        uint32_t len;
        if (off + 6 > size) return false;
        len = rd_be32(data + off + 2);
        off += 6;
        if (off + (int)len > size) return false;
        off += (int)len;
    }

    return true;
}

static const jmethod_t *jvm_find_method(const jclass_t *cls, const char *name, const char *desc, bool require_static) {
    if (!cls) return (void*)0;
    for (uint16_t i = 0; i < cls->methods_count; i++) {
        const jmethod_t *m = &cls->methods[i];
        if (require_static && ((m->access_flags & 0x0008u) == 0)) continue;
        if (!cp_utf8_eq(cls, m->name_index, name)) continue;
        if (!cp_utf8_eq(cls, m->desc_index, desc)) continue;
        return m;
    }
    return (void*)0;
}

static bool jvm_get_this_class_name(const jclass_t *cls, const char **out_ptr, uint16_t *out_len) {
    if (!cls || !out_ptr || !out_len) return false;
    return cp_class_name(cls, cls->this_class, out_ptr, out_len);
}

static bool jvm_class_name_eq(const jclass_t *cls, const char *name, uint16_t name_len) {
    const char *this_name;
    uint16_t this_len;

    if (!jvm_get_this_class_name(cls, &this_name, &this_len)) return false;
    if (this_len != name_len) return false;
    return memcmp(this_name, name, name_len) == 0;
}

static const jmethod_t *jvm_find_method_bytes(const jclass_t *cls,
                                              const char *name, uint16_t name_len,
                                              const char *desc, uint16_t desc_len,
                                              bool require_static) {
    if (!cls) return (void*)0;

    for (uint16_t i = 0; i < cls->methods_count; i++) {
        const jmethod_t *m = &cls->methods[i];
        const char *cand_name;
        const char *cand_desc;
        uint16_t cand_name_len;
        uint16_t cand_desc_len;

        if (require_static && ((m->access_flags & 0x0008u) == 0)) continue;
        if (!cp_utf8(cls, m->name_index, &cand_name, &cand_name_len)) continue;
        if (!cp_utf8(cls, m->desc_index, &cand_desc, &cand_desc_len)) continue;

        if (cand_name_len == name_len && cand_desc_len == desc_len &&
            memcmp(cand_name, name, name_len) == 0 && memcmp(cand_desc, desc, desc_len) == 0) {
            return m;
        }
    }

    return (void*)0;
}

static bool jvm_load_class_from_jar(const jvm_ctx_t *ctx,
                                    const char *class_name, uint16_t class_name_len,
                                    jclass_t *out_cls) {
    char entry_name[160];
    int n;
    const uint8_t *class_payload;
    uint32_t class_len;
    uint16_t class_method;

    if (!ctx || !ctx->rt || !ctx->rt->jar_data || ctx->rt->jar_size <= 0 || !out_cls) return false;
    if (!class_name || class_name_len == 0) return false;
    if ((int)class_name_len + 7 >= (int)sizeof(entry_name)) return false;

    memcpy(entry_name, class_name, class_name_len);
    n = (int)class_name_len;
    entry_name[n++] = '.';
    entry_name[n++] = 'c';
    entry_name[n++] = 'l';
    entry_name[n++] = 'a';
    entry_name[n++] = 's';
    entry_name[n++] = 's';
    entry_name[n] = '\0';

    if (!java_zip_find_entry(ctx->rt->jar_data, ctx->rt->jar_size, entry_name,
                             &class_payload, &class_len, &class_method)) {
        return false;
    }

    if (class_method != 0) {
        term_puts("java: referenced class is compressed; need STORED entries\n");
        return false;
    }

    return jvm_parse_class(class_payload, (int)class_len, out_cls);
}

static int jvm_desc_args(const char *desc, uint16_t len, char *ret_kind) {
    int args = 0;
    int i;

    if (ret_kind) *ret_kind = '?';
    if (!desc || len < 3 || desc[0] != '(') return -1;

    i = 1;
    while (i < len && desc[i] != ')') {
        while (i < len && desc[i] == '[') i++;
        if (i >= len) return -1;

        if (desc[i] == 'L') {
            i++;
            while (i < len && desc[i] != ';') i++;
            if (i >= len) return -1;
            i++;
            args++;
            continue;
        }

        switch (desc[i]) {
            case 'B': case 'C': case 'D': case 'F':
            case 'I': case 'J': case 'S': case 'Z':
                args++;
                i++;
                break;
            default:
                return -1;
        }
    }

    if (i >= len || desc[i] != ')') return -1;
    i++;
    if (i >= len) return -1;

    if (ret_kind) {
        if (desc[i] == 'V') *ret_kind = 'V';
        else if (desc[i] == 'L' || desc[i] == '[') *ret_kind = 'A';
        else *ret_kind = 'I';
    }

    return args;
}

static void jvm_print_string_ref(const jclass_t *cls, int32_t ref) {
    uint16_t utf8_idx;
    const char *sptr;
    uint16_t slen;

    if (ref == 0) {
        term_puts("null");
        return;
    }

    if ((ref & JVM_REF_STRING_FLAG) == 0) {
        term_puts("<non-string-ref>");
        return;
    }

    utf8_idx = (uint16_t)(ref & 0xFFFF);
    if (!cp_utf8(cls, utf8_idx, &sptr, &slen)) {
        term_puts("<bad-string-ref>");
        return;
    }

    term_print_bytes(sptr, slen);
}

static bool jvm_push_ldc(const jclass_t *cls, uint16_t idx, int32_t *stack, int *sp) {
    const jcp_entry_t *e;

    if (!cls || !stack || !sp || idx == 0 || idx >= cls->cp_count) return false;
    if (*sp >= JVM_STACK_MAX) return false;

    e = &cls->cp[idx];
    if (e->tag == 3) {
        stack[(*sp)++] = (int32_t)e->a;
        return true;
    }
    if (e->tag == 8) {
        stack[(*sp)++] = (int32_t)(JVM_REF_STRING_FLAG | ((int32_t)e->a & 0xFFFF));
        return true;
    }

    return false;
}

static bool jvm_exec_method(jvm_ctx_t *ctx, const jmethod_t *m,
                            const int32_t *args, int arg_count,
                            int32_t *ret, bool *has_ret) {
    int32_t stack[JVM_STACK_MAX];
    int32_t locals[JVM_LOCALS_MAX];
    int sp = 0;
    int pc = 0;

    if (!ctx || !ctx->cls || !m || !m->code || m->code_len == 0) {
        term_puts("java: invalid method body\n");
        return false;
    }

    if (ctx->depth > JVM_CALL_DEPTH_MAX) {
        term_puts("java: call depth exceeded\n");
        return false;
    }

    memset(stack, 0, sizeof(stack));
    memset(locals, 0, sizeof(locals));

    if (arg_count > JVM_LOCALS_MAX) {
        term_puts("java: too many method args\n");
        return false;
    }
    for (int i = 0; i < arg_count; i++) locals[i] = args[i];

    while (pc < (int)m->code_len) {
        int op_pc = pc;
        uint8_t op = m->code[pc++];

        switch (op) {
            case 0x00:
                break;
            case 0x01:
                if (sp >= JVM_STACK_MAX) return false;
                stack[sp++] = 0;
                break;
            case 0x02: case 0x03: case 0x04: case 0x05:
            case 0x06: case 0x07: case 0x08:
                if (sp >= JVM_STACK_MAX) return false;
                stack[sp++] = (int32_t)op - 0x03;
                break;
            case 0x10:
                if (pc >= (int)m->code_len || sp >= JVM_STACK_MAX) return false;
                stack[sp++] = (int32_t)(int8_t)m->code[pc++];
                break;
            case 0x11: {
                int16_t v;
                if (pc + 2 > (int)m->code_len || sp >= JVM_STACK_MAX) return false;
                v = (int16_t)rd_be16(m->code + pc);
                pc += 2;
                stack[sp++] = (int32_t)v;
                break;
            }
            case 0x12: {
                uint8_t idx;
                if (pc >= (int)m->code_len) return false;
                idx = m->code[pc++];
                if (!jvm_push_ldc(ctx->cls, idx, stack, &sp)) {
                    term_puts("java: unsupported ldc constant\n");
                    return false;
                }
                break;
            }
            case 0x13: {
                uint16_t idx;
                if (pc + 2 > (int)m->code_len) return false;
                idx = rd_be16(m->code + pc);
                pc += 2;
                if (!jvm_push_ldc(ctx->cls, idx, stack, &sp)) {
                    term_puts("java: unsupported ldc_w constant\n");
                    return false;
                }
                break;
            }
            case 0x15: {
                uint8_t idx;
                if (pc >= (int)m->code_len || sp >= JVM_STACK_MAX) return false;
                idx = m->code[pc++];
                if (idx >= JVM_LOCALS_MAX) return false;
                stack[sp++] = locals[idx];
                break;
            }
            case 0x19: {
                uint8_t idx;
                if (pc >= (int)m->code_len || sp >= JVM_STACK_MAX) return false;
                idx = m->code[pc++];
                if (idx >= JVM_LOCALS_MAX) return false;
                stack[sp++] = locals[idx];
                break;
            }
            case 0x1a: case 0x1b: case 0x1c: case 0x1d: {
                uint8_t idx = (uint8_t)(op - 0x1a);
                if (sp >= JVM_STACK_MAX || idx >= JVM_LOCALS_MAX) return false;
                stack[sp++] = locals[idx];
                break;
            }
            case 0x2a: case 0x2b: case 0x2c: case 0x2d: {
                uint8_t idx = (uint8_t)(op - 0x2a);
                if (sp >= JVM_STACK_MAX || idx >= JVM_LOCALS_MAX) return false;
                stack[sp++] = locals[idx];
                break;
            }
            case 0x36: {
                uint8_t idx;
                if (pc >= (int)m->code_len || sp < 1) return false;
                idx = m->code[pc++];
                if (idx >= JVM_LOCALS_MAX) return false;
                locals[idx] = stack[--sp];
                break;
            }
            case 0x3a: {
                uint8_t idx;
                if (pc >= (int)m->code_len || sp < 1) return false;
                idx = m->code[pc++];
                if (idx >= JVM_LOCALS_MAX) return false;
                locals[idx] = stack[--sp];
                break;
            }
            case 0x3b: case 0x3c: case 0x3d: case 0x3e: {
                uint8_t idx = (uint8_t)(op - 0x3b);
                if (sp < 1 || idx >= JVM_LOCALS_MAX) return false;
                locals[idx] = stack[--sp];
                break;
            }
            case 0x4b: case 0x4c: case 0x4d: case 0x4e: {
                uint8_t idx = (uint8_t)(op - 0x4b);
                if (sp < 1 || idx >= JVM_LOCALS_MAX) return false;
                locals[idx] = stack[--sp];
                break;
            }
            case 0x57:
                if (sp < 1) return false;
                sp--;
                break;
            case 0x59:
                if (sp < 1 || sp >= JVM_STACK_MAX) return false;
                stack[sp] = stack[sp - 1];
                sp++;
                break;
            case 0x5a:
                if (sp < 2 || sp >= JVM_STACK_MAX) return false;
                stack[sp] = stack[sp - 1];
                stack[sp - 1] = stack[sp - 2];
                stack[sp - 2] = stack[sp];
                sp++;
                break;
            case 0x5f: {
                int32_t b;
                int32_t a;
                if (sp < 2) return false;
                b = stack[--sp];
                a = stack[--sp];
                stack[sp++] = b;
                stack[sp++] = a;
                break;
            }
            case 0x60:
            case 0x64:
            case 0x68:
            case 0x6c: {
                int32_t b;
                int32_t a;
                if (sp < 2) return false;
                b = stack[--sp];
                a = stack[--sp];
                if (op == 0x60) stack[sp++] = a + b;
                if (op == 0x64) stack[sp++] = a - b;
                if (op == 0x68) stack[sp++] = a * b;
                if (op == 0x6c) {
                    if (b == 0) {
                        term_puts("java: division by zero\n");
                        return false;
                    }
                    stack[sp++] = a / b;
                }
                break;
            }
            case 0x70: {
                int32_t b;
                int32_t a;
                if (sp < 2) return false;
                b = stack[--sp];
                a = stack[--sp];
                if (b == 0) {
                    term_puts("java: modulo by zero\n");
                    return false;
                }
                stack[sp++] = a % b;
                break;
            }
            case 0x74:
                if (sp < 1) return false;
                stack[sp - 1] = -stack[sp - 1];
                break;
            case 0x78: case 0x7a: case 0x7c:
            case 0x7e: case 0x80: case 0x82: {
                int32_t b;
                int32_t a;
                if (sp < 2) return false;
                b = stack[--sp];
                a = stack[--sp];

                if (op == 0x78) stack[sp++] = a << (b & 0x1F);
                if (op == 0x7a) stack[sp++] = a >> (b & 0x1F);
                if (op == 0x7c) stack[sp++] = ((uint32_t)a) >> (b & 0x1F);
                if (op == 0x7e) stack[sp++] = a & b;
                if (op == 0x80) stack[sp++] = a | b;
                if (op == 0x82) stack[sp++] = a ^ b;
                break;
            }
            case 0x84: {
                uint8_t idx;
                int8_t c;
                if (pc + 2 > (int)m->code_len) return false;
                idx = m->code[pc++];
                c = (int8_t)m->code[pc++];
                if (idx >= JVM_LOCALS_MAX) return false;
                locals[idx] += c;
                break;
            }
            case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: case 0x9e: {
                int16_t rel;
                int32_t v;
                bool take = false;
                if (pc + 2 > (int)m->code_len || sp < 1) return false;
                rel = (int16_t)rd_be16(m->code + pc);
                pc += 2;
                v = stack[--sp];
                if (op == 0x99) take = (v == 0);
                if (op == 0x9a) take = (v != 0);
                if (op == 0x9b) take = (v < 0);
                if (op == 0x9c) take = (v >= 0);
                if (op == 0x9d) take = (v > 0);
                if (op == 0x9e) take = (v <= 0);
                if (take) {
                    int dst = op_pc + rel;
                    if (dst < 0 || dst >= (int)m->code_len) return false;
                    pc = dst;
                }
                break;
            }
            case 0x9f: case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4: {
                int16_t rel;
                int32_t b;
                int32_t a;
                bool take = false;
                if (pc + 2 > (int)m->code_len || sp < 2) return false;
                rel = (int16_t)rd_be16(m->code + pc);
                pc += 2;
                b = stack[--sp];
                a = stack[--sp];
                if (op == 0x9f) take = (a == b);
                if (op == 0xa0) take = (a != b);
                if (op == 0xa1) take = (a < b);
                if (op == 0xa2) take = (a >= b);
                if (op == 0xa3) take = (a > b);
                if (op == 0xa4) take = (a <= b);
                if (take) {
                    int dst = op_pc + rel;
                    if (dst < 0 || dst >= (int)m->code_len) return false;
                    pc = dst;
                }
                break;
            }
            case 0xa5:
            case 0xa6: {
                int16_t rel;
                int32_t b;
                int32_t a;
                bool take;

                if (pc + 2 > (int)m->code_len || sp < 2) return false;
                rel = (int16_t)rd_be16(m->code + pc);
                pc += 2;
                b = stack[--sp];
                a = stack[--sp];
                take = (op == 0xa5) ? (a == b) : (a != b);
                if (take) {
                    int dst = op_pc + rel;
                    if (dst < 0 || dst >= (int)m->code_len) return false;
                    pc = dst;
                }
                break;
            }
            case 0xa7: {
                int16_t rel;
                int dst;
                if (pc + 2 > (int)m->code_len) return false;
                rel = (int16_t)rd_be16(m->code + pc);
                pc += 2;
                dst = op_pc + rel;
                if (dst < 0 || dst >= (int)m->code_len) return false;
                pc = dst;
                break;
            }
            case 0xc8: {
                int32_t rel;
                int dst;
                if (pc + 4 > (int)m->code_len) return false;
                rel = (int32_t)rd_be32(m->code + pc);
                pc += 4;
                dst = op_pc + rel;
                if (dst < 0 || dst >= (int)m->code_len) return false;
                pc = dst;
                break;
            }
            case 0xc6:
            case 0xc7: {
                int16_t rel;
                int32_t v;
                bool take;

                if (pc + 2 > (int)m->code_len || sp < 1) return false;
                rel = (int16_t)rd_be16(m->code + pc);
                pc += 2;
                v = stack[--sp];
                take = (op == 0xc6) ? (v == 0) : (v != 0);

                if (take) {
                    int dst = op_pc + rel;
                    if (dst < 0 || dst >= (int)m->code_len) return false;
                    pc = dst;
                }
                break;
            }
            case 0xac:
                if (sp < 1) return false;
                if (ret) *ret = stack[--sp];
                if (has_ret) *has_ret = true;
                return true;
            case 0xb0:
                if (sp < 1) return false;
                if (ret) *ret = stack[--sp];
                if (has_ret) *has_ret = true;
                return true;
            case 0xb1:
                if (has_ret) *has_ret = false;
                return true;
            case 0xb2: {
                uint16_t idx;
                const char *cls_name;
                const char *fld_name;
                const char *fld_desc;
                uint16_t cls_len;
                uint16_t fld_len;
                uint16_t desc_len;

                if (pc + 2 > (int)m->code_len || sp >= JVM_STACK_MAX) return false;
                idx = rd_be16(m->code + pc);
                pc += 2;

                if (!cp_resolve_ref(ctx->cls, idx, 9, &cls_name, &cls_len, &fld_name, &fld_len, &fld_desc, &desc_len)) {
                    term_puts("java: bad fieldref in getstatic\n");
                    return false;
                }

                if (bytes_eq_lit(cls_name, cls_len, "java/lang/System") &&
                    (bytes_eq_lit(fld_name, fld_len, "out") || bytes_eq_lit(fld_name, fld_len, "err")) &&
                    bytes_eq_lit(fld_desc, desc_len, "Ljava/io/PrintStream;")) {
                    stack[sp++] = JVM_REF_PRINTSTREAM;
                } else {
                    term_puts("java: unsupported getstatic target\n");
                    return false;
                }
                break;
            }
            case 0xb6: {
                uint16_t idx;
                const char *cls_name;
                const char *m_name;
                const char *m_desc;
                uint16_t cls_len;
                uint16_t name_len;
                uint16_t desc_len;
                char ret_kind;
                int argc;
                int32_t argv[8];
                int32_t obj;

                if (pc + 2 > (int)m->code_len) return false;
                idx = rd_be16(m->code + pc);
                pc += 2;

                if (!cp_resolve_ref(ctx->cls, idx, 10, &cls_name, &cls_len, &m_name, &name_len, &m_desc, &desc_len)) {
                    term_puts("java: bad methodref in invokevirtual\n");
                    return false;
                }

                argc = jvm_desc_args(m_desc, desc_len, &ret_kind);
                if (argc < 0 || argc > 8 || sp < argc + 1) return false;
                for (int i = argc - 1; i >= 0; i--) argv[i] = stack[--sp];
                obj = stack[--sp];

                if (bytes_eq_lit(cls_name, cls_len, "java/io/PrintStream") &&
                    (bytes_eq_lit(m_name, name_len, "print") || bytes_eq_lit(m_name, name_len, "println")) &&
                    obj == JVM_REF_PRINTSTREAM) {
                    bool newline = bytes_eq_lit(m_name, name_len, "println");

                    if (bytes_eq_lit(m_desc, desc_len, "()V")) {
                        /* nothing */
                    } else if (bytes_eq_lit(m_desc, desc_len, "(I)V") ||
                               bytes_eq_lit(m_desc, desc_len, "(S)V") ||
                               bytes_eq_lit(m_desc, desc_len, "(B)V") ||
                               bytes_eq_lit(m_desc, desc_len, "(C)V") ||
                               bytes_eq_lit(m_desc, desc_len, "(Z)V")) {
                        if (argc != 1) return false;
                        if (bytes_eq_lit(m_desc, desc_len, "(Z)V")) {
                            term_puts(argv[0] ? "true" : "false");
                        } else if (bytes_eq_lit(m_desc, desc_len, "(C)V")) {
                            term_putc((char)argv[0], 0x07);
                        } else {
                            term_print_i32(argv[0]);
                        }
                    } else if (bytes_eq_lit(m_desc, desc_len, "(Ljava/lang/String;)V")) {
                        if (argc != 1) return false;
                        jvm_print_string_ref(ctx->cls, argv[0]);
                    } else {
                        term_puts("java: unsupported PrintStream signature\n");
                        return false;
                    }

                    if (newline) term_puts("\n");
                } else {
                    term_puts("java: unsupported invokevirtual target\n");
                    return false;
                }
                break;
            }
            case 0xb7: {
                uint16_t idx;
                const char *cls_name;
                const char *m_name;
                const char *m_desc;
                uint16_t cls_len;
                uint16_t name_len;
                uint16_t desc_len;
                char ret_kind;
                int argc;
                int32_t argv[8];
                int32_t obj;

                if (pc + 2 > (int)m->code_len) return false;
                idx = rd_be16(m->code + pc);
                pc += 2;

                if (!cp_resolve_ref(ctx->cls, idx, 10, &cls_name, &cls_len, &m_name, &name_len, &m_desc, &desc_len)) {
                    term_puts("java: bad methodref in invokespecial\n");
                    return false;
                }

                argc = jvm_desc_args(m_desc, desc_len, &ret_kind);
                if (argc < 0 || argc > 8 || sp < argc + 1) return false;
                for (int i = argc - 1; i >= 0; i--) argv[i] = stack[--sp];
                obj = stack[--sp];

                if ((obj & JVM_REF_OBJECT_FLAG) == 0) {
                    term_puts("java: invokespecial on non-object\n");
                    return false;
                }

                if (bytes_eq_lit(m_name, name_len, "<init>")) {
                    if (ret_kind != 'V') return false;
                    break;
                }

                term_puts("java: unsupported invokespecial target\n");
                return false;
            }
            case 0xbb: {
                uint16_t idx;
                if (pc + 2 > (int)m->code_len || sp >= JVM_STACK_MAX) return false;
                idx = rd_be16(m->code + pc);
                pc += 2;
                if (idx == 0 || idx >= ctx->cls->cp_count || ctx->cls->cp[idx].tag != 7) {
                    term_puts("java: bad class in new\n");
                    return false;
                }
                stack[sp++] = (int32_t)(JVM_REF_OBJECT_FLAG | (idx & 0xFFFF));
                break;
            }
            case 0xb8: {
                uint16_t idx;
                const char *cls_name;
                const char *m_name;
                const char *m_desc;
                uint16_t cls_len;
                uint16_t name_len;
                uint16_t desc_len;
                char ret_kind;
                int argc;
                int32_t argv[8];
                int32_t child_ret = 0;
                bool child_has_ret = false;
                jclass_t ext_cls;
                const jclass_t *target_cls;
                const jmethod_t *target;

                if (pc + 2 > (int)m->code_len) return false;
                idx = rd_be16(m->code + pc);
                pc += 2;

                if (!cp_resolve_ref(ctx->cls, idx, 10, &cls_name, &cls_len, &m_name, &name_len, &m_desc, &desc_len)) {
                    term_puts("java: bad methodref in invokestatic\n");
                    return false;
                }

                argc = jvm_desc_args(m_desc, desc_len, &ret_kind);
                if (argc < 0 || argc > 8 || sp < argc) return false;
                for (int i = argc - 1; i >= 0; i--) argv[i] = stack[--sp];

                if (jvm_class_name_eq(ctx->cls, cls_name, cls_len)) {
                    target_cls = ctx->cls;
                } else {
                    if (!jvm_load_class_from_jar(ctx, cls_name, cls_len, &ext_cls)) {
                        term_puts("java: target class not found in jar\n");
                        return false;
                    }
                    target_cls = &ext_cls;
                }

                target = jvm_find_method_bytes(target_cls, m_name, name_len, m_desc, desc_len, true);
                if (!target) {
                    term_puts("java: target static method not found\n");
                    return false;
                }

                jvm_ctx_t child = *ctx;
                child.cls = target_cls;
                child.depth++;
                if (!jvm_exec_method(&child, target, argv, argc, &child_ret, &child_has_ret)) {
                    return false;
                }

                if (ret_kind != 'V') {
                    if (!child_has_ret) {
                        term_puts("java: missing return value\n");
                        return false;
                    }
                    if (sp >= JVM_STACK_MAX) return false;
                    stack[sp++] = child_ret;
                }

                break;
            }
            default:
                term_puts("java: unsupported opcode 0x");
                term_print_u32(op, 16);
                term_puts("\n");
                return false;
        }
    }

    if (has_ret) *has_ret = false;
    return true;
}

static bool jvm_execute_class_with_rt(const uint8_t *data, int size, const jvm_runtime_t *rt) {
    jclass_t cls;
    const jmethod_t *main_m;
    jvm_ctx_t ctx;
    int32_t ret = 0;
    bool has_ret = false;

    if (!jvm_parse_class(data, size, &cls)) {
        term_puts("java: class parse failed\n");
        return false;
    }

    term_puts("java: TinyJVM class mode (extended)\n");
    term_puts("  ClassFile version ");
    term_print_u32(cls.major, 10);
    term_putc('.', 0x07);
    term_print_u32(cls.minor, 10);
    term_puts("\n");

    main_m = jvm_find_method(&cls, "main", "([Ljava/lang/String;)V", true);
    if (!main_m) {
        main_m = jvm_find_method(&cls, "main", "()V", true);
    }

    if (!main_m) {
        term_puts("java: no supported static main method found\n");
        return false;
    }

    ctx.cls = &cls;
    ctx.rt = rt;
    ctx.depth = 0;

    if (cp_utf8_eq(&cls, main_m->desc_index, "([Ljava/lang/String;)V")) {
        int32_t argv0[1] = {0};
        if (!jvm_exec_method(&ctx, main_m, argv0, 1, &ret, &has_ret)) {
            return false;
        }
    } else {
        if (!jvm_exec_method(&ctx, main_m, (void*)0, 0, &ret, &has_ret)) {
            return false;
        }
    }

    return true;
}

static bool jvm_execute_class(const uint8_t *data, int size) {
    return jvm_execute_class_with_rt(data, size, (void*)0);
}

/*
 * 0 = unknown
 * 1 = HJAR (HeatOS mini format)
 * 2 = ZIP/JAR
 * 3 = JVM ClassFile
 */
static int java_detect_format(const uint8_t *data, int size) {
    if (!data || size < 4) return 0;
    if (size >= 5 && data[0] == 'H' && data[1] == 'J' && data[2] == 'A' && data[3] == 'R') return 1;
    if (data[0] == 'P' && data[1] == 'K' && data[2] == 0x03 && data[3] == 0x04) return 2;
    if (rd_be32(data) == 0xCAFEBABEu) return 3;
    return 0;
}

static bool java_file_exists(const char *path) {
    fs_node_t node;
    return path && *path && fs_resolve_checked(path, &node) && fs_is_file(node);
}

static bool java_copy_path(char *dst, int dst_cap, const char *src) {
    size_t n;
    if (!dst || dst_cap <= 0 || !src) return false;
    n = strlen(src);
    if ((int)n + 1 > dst_cap) return false;
    memcpy(dst, src, n + 1);
    return true;
}

static bool java_path_has_extension(const char *path) {
    int i;
    if (!path) return false;
    i = (int)strlen(path) - 1;
    while (i >= 0 && path[i] != '/') {
        if (path[i] == '.') return true;
        i--;
    }
    return false;
}

static bool java_join_path(char *out, int out_cap, const char *dir, const char *name) {
    int pos = 0;
    size_t dir_len;
    size_t name_len;
    bool need_sep;

    if (!out || out_cap <= 0 || !dir || !name) return false;

    dir_len = strlen(dir);
    name_len = strlen(name);
    need_sep = (dir_len > 0 && dir[dir_len - 1] != '/');

    if ((int)(dir_len + name_len + (need_sep ? 1 : 0) + 1) > out_cap) return false;

    if (dir_len > 0) {
        memcpy(out + pos, dir, dir_len);
        pos += (int)dir_len;
    }

    if (need_sep) out[pos++] = '/';

    if (name_len > 0) {
        memcpy(out + pos, name, name_len);
        pos += (int)name_len;
    }

    out[pos] = '\0';
    return true;
}

static bool java_try_base_and_ext(const char *base, char *resolved, int resolved_cap) {
    static const char *exts[] = {".jar", ".class", ".hjar"};
    char candidate[160];

    if (!base || !*base || !resolved || resolved_cap <= 0) return false;

    if (java_file_exists(base)) {
        return java_copy_path(resolved, resolved_cap, base);
    }

    if (java_path_has_extension(base)) return false;

    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        size_t base_len = strlen(base);
        size_t ext_len = strlen(exts[i]);
        if (base_len + ext_len + 1 > sizeof(candidate)) continue;

        memcpy(candidate, base, base_len);
        memcpy(candidate + base_len, exts[i], ext_len + 1);

        if (java_file_exists(candidate)) {
            return java_copy_path(resolved, resolved_cap, candidate);
        }
    }

    return false;
}

static const char *java_resolve_path(const char *raw_path, bool *out_used_fallback) {
    static char resolved[160];
    static const char *search_dirs[] = {"/java", "/home", "/docs"};
    char cwd[64];
    char candidate[160];
    const char *path;
    const char *fallback;

    if (out_used_fallback) *out_used_fallback = false;

    path = raw_path;
    while (path && path[0] == '.' && path[1] == '/') {
        path += 2;
    }

    if (!path || !*path) {
        if (out_used_fallback) *out_used_fallback = true;
        return java_default_path();
    }

    if (java_try_base_and_ext(path, resolved, (int)sizeof(resolved))) {
        return resolved;
    }

    if (path[0] != '/') {
        fs_build_path(fs_cwd_get(), cwd, sizeof(cwd));
        if (java_join_path(candidate, (int)sizeof(candidate), cwd, path) &&
            java_try_base_and_ext(candidate, resolved, (int)sizeof(resolved))) {
            return resolved;
        }

        for (size_t i = 0; i < sizeof(search_dirs) / sizeof(search_dirs[0]); i++) {
            if (java_join_path(candidate, (int)sizeof(candidate), search_dirs[i], path) &&
                java_try_base_and_ext(candidate, resolved, (int)sizeof(resolved))) {
                return resolved;
            }
        }
    }

    fallback = java_default_path();
    if (fallback && out_used_fallback) *out_used_fallback = true;
    return fallback;
}

static bool java_load_file(const char *path, uint8_t *buf, int cap, int *out_n, int *out_fmt) {
    fs_node_t node;
    int n;

    *out_n = -1;
    *out_fmt = 0;

    if (!path || !*path) {
        term_puts("java: missing file path\n");
        return false;
    }

    if (!fs_resolve_checked(path, &node) || !fs_is_file(node)) {
        term_puts("java: file not found: ");
        term_puts(path);
        term_puts("\n");
        return false;
    }

    n = fs_read(node, buf, cap);
    if (n <= 0) {
        term_puts("java: failed to read file\n");
        return false;
    }

    *out_n = n;
    *out_fmt = java_detect_format(buf, n);
    return true;
}

static bool java_zip_next_entry(const uint8_t *data, int size, int *io_off,
                                const uint8_t **out_name, uint16_t *out_name_len,
                                const uint8_t **out_payload, uint32_t *out_payload_len,
                                uint16_t *out_method) {
    int off = *io_off;
    uint32_t sig;
    uint16_t flags;
    uint16_t method;
    uint32_t comp_size;
    uint16_t name_len;
    uint16_t extra_len;
    int name_off;
    int data_off;

    if (off + 30 > size) return false;

    sig = rd_le32(data + off);
    if (sig != 0x04034B50u) {
        return false;
    }

    flags = rd_le16(data + off + 6);
    method = rd_le16(data + off + 8);
    comp_size = rd_le32(data + off + 18);
    name_len = rd_le16(data + off + 26);
    extra_len = rd_le16(data + off + 28);

    name_off = off + 30;
    data_off = name_off + name_len + extra_len;

    if (name_off + name_len > size || data_off > size || data_off + (int)comp_size > size) {
        return false;
    }

    if ((flags & 0x0008u) != 0u) {
        return false;
    }

    *out_name = data + name_off;
    *out_name_len = name_len;
    *out_payload = data + data_off;
    *out_payload_len = comp_size;
    *out_method = method;
    *io_off = data_off + (int)comp_size;
    return true;
}

static bool java_zip_find_entry(const uint8_t *data, int size, const char *target,
                                const uint8_t **out_payload, uint32_t *out_payload_len,
                                uint16_t *out_method) {
    int off = 0;
    const uint8_t *name;
    uint16_t name_len;
    const uint8_t *payload;
    uint32_t payload_len;
    uint16_t method;
    size_t target_len = strlen(target);

    while (java_zip_next_entry(data, size, &off, &name, &name_len, &payload, &payload_len, &method)) {
        if (name_len == target_len && memcmp(name, target, target_len) == 0) {
            *out_payload = payload;
            *out_payload_len = payload_len;
            *out_method = method;
            return true;
        }
    }

    return false;
}

static bool java_manifest_main_class(const uint8_t *payload, uint32_t payload_len,
                                     char *out_path, int out_cap) {
    const char key[] = "Main-Class:";
    int i = 0;

    if (!payload || !out_path || out_cap < 8) return false;

    while (i < (int)payload_len) {
        int start = i;
        int end;
        int p;
        int n;

        while (i < (int)payload_len && payload[i] != '\n' && payload[i] != '\r') i++;
        end = i;
        while (i < (int)payload_len && (payload[i] == '\n' || payload[i] == '\r')) i++;

        if (end - start < (int)strlen(key)) continue;
        if (memcmp(payload + start, key, strlen(key)) != 0) continue;

        p = start + (int)strlen(key);
        while (p < end && (payload[p] == ' ' || payload[p] == '\t')) p++;

        n = 0;
        while (p < end && n + 7 < out_cap) {
            char ch = (char)payload[p++];
            if (ch == '.') ch = '/';
            out_path[n++] = ch;
        }

        if (n == 0) return false;
        if (n + 6 >= out_cap) return false;

        out_path[n++] = '.';
        out_path[n++] = 'c';
        out_path[n++] = 'l';
        out_path[n++] = 'a';
        out_path[n++] = 's';
        out_path[n++] = 's';
        out_path[n] = '\0';
        return true;
    }

    return false;
}

static bool java_zip_find_first_class(const uint8_t *data, int size,
                                      const uint8_t **out_payload, uint32_t *out_payload_len,
                                      uint16_t *out_method, char *name_buf, int name_cap) {
    int off = 0;
    const uint8_t *name;
    uint16_t name_len;
    const uint8_t *payload;
    uint32_t payload_len;
    uint16_t method;

    while (java_zip_next_entry(data, size, &off, &name, &name_len, &payload, &payload_len, &method)) {
        if (!ends_with_bytes(name, name_len, ".class")) continue;

        if (name_buf && name_cap > 0) {
            int n = (name_len < (uint16_t)(name_cap - 1)) ? name_len : (name_cap - 1);
            memcpy(name_buf, name, n);
            name_buf[n] = '\0';
        }

        *out_payload = payload;
        *out_payload_len = payload_len;
        *out_method = method;
        return true;
    }

    return false;
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
                term_putc((char)data[pc + i], 0x07);
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
            term_puts("\n");
            return true;
        }

        term_puts("java: unknown HJAR opcode 0x");
        term_print_u32(op, 16);
        term_puts("\n");
        return false;
    }

    term_puts("java: HJAR ended without return\n");
    return false;
}

static bool java_zip_list_entries(const uint8_t *data, int size) {
    int off = 0;
    int entries = 0;
    const uint8_t *name;
    uint16_t name_len;
    const uint8_t *payload;
    uint32_t payload_len;
    uint16_t method;

    while (java_zip_next_entry(data, size, &off, &name, &name_len, &payload, &payload_len, &method)) {
        (void)payload;
        term_puts("  - ");
        term_print_bytes((const char*)name, name_len);
        term_puts("  method=");
        term_print_u32(method, 10);
        term_puts("  bytes=");
        term_print_u32(payload_len, 10);
        term_puts("\n");
        entries++;
    }

    if (entries == 0) {
        term_puts("jarinfo: no local file headers found\n");
        return false;
    }

    term_puts("jarinfo: entries parsed=");
    term_print_u32((uint32_t)entries, 10);
    term_puts("\n");
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
    term_putc('.', 0x07);
    term_print_u32(minor, 10);
    term_puts("  cp_count=");
    term_print_u32(cp_count, 10);
    term_puts("\n");
    term_puts("Execution path: TinyJVM extended mode (class + jar + cross-class static calls).\n");
}

static bool java_run_jar(const uint8_t *data, int size) {
    jvm_runtime_t rt;
    const uint8_t *manifest_payload;
    uint32_t manifest_len;
    uint16_t manifest_method;

    const uint8_t *class_payload = (void*)0;
    uint32_t class_len = 0;
    uint16_t class_method = 0;

    char class_path[128];
    bool have_target = false;

    if (java_zip_find_entry(data, size, "META-INF/MANIFEST.MF", &manifest_payload, &manifest_len, &manifest_method)) {
        if (manifest_method == 0 && java_manifest_main_class(manifest_payload, manifest_len, class_path, sizeof(class_path))) {
            have_target = true;
        }
    }

    if (have_target) {
        if (!java_zip_find_entry(data, size, class_path, &class_payload, &class_len, &class_method)) {
            term_puts("java: Main-Class entry not found in jar\n");
            return false;
        }
        term_puts("java: jar Main-Class => ");
        term_puts(class_path);
        term_puts("\n");
    } else {
        char first_name[128];
        if (!java_zip_find_first_class(data, size, &class_payload, &class_len, &class_method, first_name, sizeof(first_name))) {
            term_puts("java: jar has no .class entry\n");
            return false;
        }
        term_puts("java: jar fallback class => ");
        term_puts(first_name);
        term_puts("\n");
    }

    if (class_method != 0) {
        term_puts("java: compressed class entries are not supported yet (need STORED, method=0)\n");
        return false;
    }

    rt.jar_data = data;
    rt.jar_size = size;
    return jvm_execute_class_with_rt(class_payload, (int)class_len, &rt);
}

static const char *java_default_path(void) {
    fs_node_t n;

    if (fs_resolve_checked("/java/tinyjvm.jar", &n) && fs_is_file(n)) return "/java/tinyjvm.jar";
    if (fs_resolve_checked("/java/hello.class", &n) && fs_is_file(n)) return "/java/hello.class";
    if (fs_resolve_checked("/java/hello.jar", &n) && fs_is_file(n)) return "/java/hello.jar";
    if (fs_resolve_checked("/java/sample.jar", &n) && fs_is_file(n)) return "/java/sample.jar";
    return (void*)0;
}

void cmd_jarinfo(const char *args) {
    uint8_t buf[JAVA_BUF_CAP];
    int n;
    int fmt;
    bool used_fallback = false;
    const char *path = java_resolve_path((args && *args) ? args : (void*)0, &used_fallback);

    if (!path) {
        term_puts("jarinfo: no default Java file found. try: ls /java\n");
        return;
    }

    if ((args && *args) && used_fallback) {
        term_puts("jarinfo: requested file not found, using ");
        term_puts(path);
        term_puts("\n");
    }

    if (!java_load_file(path, buf, (int)sizeof(buf), &n, &fmt)) {
        return;
    }

    if (fmt == 1) {
        term_puts("jarinfo: HJAR (HeatOS mini format), version=");
        term_print_u32(buf[4], 10);
        term_puts(" size=");
        term_print_u32((uint32_t)n, 10);
        term_puts("\n");
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

void cmd_java(const char *args) {
    uint8_t buf[JAVA_BUF_CAP];
    int n;
    int fmt;
    bool used_fallback = false;
    const char *path = java_resolve_path((args && *args) ? args : (void*)0, &used_fallback);

    if (!path) {
        term_puts("java: no default Java file found. try: ls /java\n");
        return;
    }

    if ((args && *args) && used_fallback) {
        term_puts("java: requested path not found, running ");
        term_puts(path);
        term_puts("\n");
    }

    if (!java_load_file(path, buf, (int)sizeof(buf), &n, &fmt)) {
        return;
    }

    if (fmt == 1) {
        term_puts("java: running HJAR app\n");
        (void)java_hjar_exec(buf, n);
        return;
    }

    if (fmt == 2) {
        term_puts("java: ZIP/JAR detected\n");
        if (!java_run_jar(buf, n)) {
            term_puts("java: jar execution failed\n");
        }
        return;
    }

    if (fmt == 3) {
        term_puts("java: direct .class input detected\n");
        java_class_info(buf, n);
        if (!jvm_execute_class(buf, n)) {
            term_puts("java: class execution failed\n");
        }
        return;
    }

    term_puts("java: unsupported file format\n");
}

void cmd_java_dir(const char *args) {
    fs_node_t node;
    (void)args;

    if (!fs_resolve_checked("/java", &node) || !fs_is_dir(node)) {
        term_puts("/java: directory not found\n");
        return;
    }

    fs_cwd_set(node);
    term_puts("cwd => /java\n");
    cmd_ls((void*)0);
}
