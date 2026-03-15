[bits 16]
[org 0x0000]

INPUT_MAX equ 63

start:
    cli
    cld
    xor ax, ax
    mov ss, ax
    mov sp, 0x9A00
    sti

    mov ax, cs
    mov ds, ax
    mov es, ax

    call clear_screen
    mov si, welcome_msg
    call print_string
    mov si, help_hint_msg
    call print_string

shell_loop:
    mov si, prompt_msg
    call print_string

    mov di, input_buffer
    mov cx, INPUT_MAX
    call read_line

    mov si, input_buffer
    call skip_spaces

    cmp byte [si], 0
    je shell_loop

    mov di, cmd_help
    call string_equals
    cmp al, 1
    je show_help

    mov di, cmd_clear
    call string_equals
    cmp al, 1
    je do_clear

    mov di, cmd_about
    call string_equals
    cmp al, 1
    je show_about

    mov di, cmd_halt
    call string_equals
    cmp al, 1
    je do_halt

    mov di, cmd_reboot
    call string_equals
    cmp al, 1
    je do_reboot

    mov si, unknown_msg
    call print_string
    jmp shell_loop

show_help:
    mov si, help_msg
    call print_string
    jmp shell_loop

do_clear:
    call clear_screen
    jmp shell_loop

show_about:
    mov si, about_msg
    call print_string
    jmp shell_loop

do_halt:
    mov si, halt_msg
    call print_string
.halt_forever:
    cli
    hlt
    jmp .halt_forever

do_reboot:
    mov si, reboot_msg
    call print_string
    int 0x19
    jmp do_halt

skip_spaces:
.skip:
    cmp byte [si], ' '
    jne .done
    inc si
    jmp .skip
.done:
    ret

; Compare two zero-terminated strings.
; IN: SI=first string, DI=second string
; OUT: AL=1 if equal, AL=0 otherwise
string_equals:
    push si
    push di
.compare:
    mov al, [si]
    mov ah, [di]
    cmp al, ah
    jne .not_equal
    cmp al, 0
    je .equal
    inc si
    inc di
    jmp .compare
.equal:
    mov al, 1
    jmp .finish
.not_equal:
    xor al, al
.finish:
    pop di
    pop si
    ret

clear_screen:
    mov ax, 0x0003
    int 0x10
    ret

print_string:
    pusha
.next_char:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x0F
    int 0x10
    jmp .next_char
.done:
    popa
    ret

print_newline:
    pusha
    mov ah, 0x0E
    mov al, 13
    int 0x10
    mov al, 10
    int 0x10
    popa
    ret

read_line:
    pusha
    xor bx, bx
.read_key:
    xor ah, ah
    int 0x16

    cmp al, 13
    je .enter

    cmp al, 8
    je .backspace

    cmp al, 32
    jb .read_key

    cmp bx, cx
    jae .read_key

    mov [di + bx], al
    inc bx

    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x0F
    int 0x10
    jmp .read_key

.backspace:
    cmp bx, 0
    je .read_key
    dec bx
    mov ah, 0x0E
    mov al, 8
    int 0x10
    mov al, ' '
    int 0x10
    mov al, 8
    int 0x10
    jmp .read_key

.enter:
    mov byte [di + bx], 0
    call print_newline
    popa
    ret

welcome_msg db "RushOS kernel booted.", 13, 10, 0
help_hint_msg db "Type 'help' to see commands.", 13, 10, 13, 10, 0
prompt_msg db "rush> ", 0
unknown_msg db "Unknown command. Type 'help'.", 13, 10, 0
help_msg db "Commands:", 13, 10
         db "  help   - list commands", 13, 10
         db "  clear  - clear the screen", 13, 10
         db "  about  - show kernel info", 13, 10
         db "  halt   - stop the CPU", 13, 10
         db "  reboot - reboot the machine", 13, 10, 13, 10, 0
about_msg db "RushOS custom kernel v0.1 (16-bit real mode).", 13, 10, 0
halt_msg db "System halted.", 13, 10, 0
reboot_msg db "Rebooting...", 13, 10, 0

cmd_help db "help", 0
cmd_clear db "clear", 0
cmd_about db "about", 0
cmd_halt db "halt", 0
cmd_reboot db "reboot", 0

input_buffer times INPUT_MAX + 1 db 0
