RAMDISK_MAX_NODES equ 64
RAMDISK_NAME_LEN equ 12
RAMDISK_DATA_CAP equ 8192

FS_TYPE_FREE equ 0
FS_TYPE_DIR equ 1
FS_TYPE_FILE equ 2

ramdisk_init:
    push ax
    push cx
    push di

    mov cx, RAMDISK_MAX_NODES
    mov di, fs_node_type
    xor al, al
    rep stosb

    mov cx, RAMDISK_MAX_NODES
    mov di, fs_node_parent
    rep stosb

    mov cx, RAMDISK_MAX_NODES
    mov di, fs_node_first_child
    rep stosb

    mov cx, RAMDISK_MAX_NODES
    mov di, fs_node_next_sibling
    rep stosb

    mov cx, RAMDISK_MAX_NODES
    mov di, fs_node_size
    xor ax, ax
    rep stosw

    mov cx, RAMDISK_MAX_NODES
    mov di, fs_node_data_off
    xor ax, ax
    rep stosw

    mov cx, RAMDISK_MAX_NODES * RAMDISK_NAME_LEN
    mov di, fs_node_name
    xor al, al
    rep stosb

    mov word [fs_data_free], 0
    mov byte [fs_cwd], 0

    mov byte [fs_node_type + 0], FS_TYPE_DIR
    mov si, fs_init_apps
    xor bx, bx
    call fs_mkdir_child
    mov si, fs_init_docs
    xor bx, bx
    call fs_mkdir_child
    mov si, fs_init_home
    xor bx, bx
    call fs_mkdir_child
    mov si, fs_init_system
    xor bx, bx
    call fs_mkdir_child

    pop di
    pop cx
    pop ax
    ret

fs_init_apps db "apps",0
fs_init_docs db "docs",0
fs_init_home db "home",0
fs_init_system db "system",0

fs_copy_name12_lower:
    push ax
    push cx
.loop:
    lodsb
    test al, al
    jz .zero
    call to_lower
    stosb
    loop .loop
    jmp .done
.zero:
    mov al, 0
    stosb
    loop .zero
.done:
    pop cx
    pop ax
    ret

fs_alloc_node:
    push cx
    push di
    mov cx, RAMDISK_MAX_NODES - 1
    mov di, fs_node_type + 1
.scan:
    cmp byte [di], FS_TYPE_FREE
    je .found
    inc di
    loop .scan
    stc
    pop di
    pop cx
    ret
.found:
    mov ax, di
    sub ax, fs_node_type
    clc
    pop di
    pop cx
    ret

fs_name_ptr:
    push bx
    xor ax, ax
    mov al, bl
    mov bx, RAMDISK_NAME_LEN
    mul bx
    mov di, fs_node_name
    add di, ax
    pop bx
    ret

fs_token_from_si:
    push ax
    push cx
    push di
    mov di, fs_token_buf
    mov cx, RAMDISK_NAME_LEN
.copy:
    mov al, [si]
    cmp al, 0
    je .done
    cmp al, ' '
    je .done
    cmp al, '/'
    je .done
    call to_lower
    stosb
    inc si
    loop .copy
.skip:
    mov al, [si]
    cmp al, 0
    je .done
    cmp al, ' '
    je .done
    cmp al, '/'
    je .done
    inc si
    jmp .skip
.done:
    mov al, 0
    stosb
    pop di
    pop cx
    pop ax
    ret

fs_token_equals:
    push ax
    push cx
    push si
    push di
    mov cx, RAMDISK_NAME_LEN
.loop:
    mov al, [si]
    mov ah, [di]
    cmp al, ah
    jne .no
    test al, al
    jz .yes
    inc si
    inc di
    loop .loop
.yes:
    mov al, 1
    jmp .done
.no:
    xor al, al
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

fs_find_child:
    push ax
    push bx
    push di
    mov dl, [fs_node_first_child + bx]
.iter:
    test dl, dl
    jz .nf
    xor bx, bx
    mov bl, dl
    call fs_name_ptr
    mov si, fs_token_buf
    call fs_token_equals
    cmp al, 1
    je .found
    mov dl, [fs_node_next_sibling + bx]
    jmp .iter
.nf:
    stc
    pop di
    pop bx
    pop ax
    ret
.found:
    mov al, dl
    clc
    pop di
    pop bx
    pop ax
    ret

fs_mkdir_child:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, fs_token_buf
    mov cx, RAMDISK_NAME_LEN
    call fs_copy_name12_lower
    call fs_find_child
    jnc .fail
    call fs_alloc_node
    jc .fail
    mov dl, al
    xor cx, cx
    mov cl, dl
    mov byte [fs_node_type + cx], FS_TYPE_DIR
    mov byte [fs_node_parent + cx], bl
    mov byte [fs_node_first_child + cx], 0
    mov byte [fs_node_next_sibling + cx], 0

    mov bl, dl
    call fs_name_ptr
    mov si, fs_token_buf
    mov cx, RAMDISK_NAME_LEN
    call fs_copy_name12_lower

    mov bl, [fs_node_parent + cx]
    xor bh, bh
    mov al, [fs_node_first_child + bx]
    mov [fs_node_next_sibling + cx], al
    mov [fs_node_first_child + bx], dl
    clc
    jmp .done
.fail:
    stc
.done:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

fs_resolve_path:
    push ax
    push bx
    push si
    cmp byte [si], '/'
    jne .rel
    xor bx, bx
    jmp .eat
.rel:
    xor bx, bx
    mov bl, [fs_cwd]
.eat:
    cmp byte [si], '/'
    jne .next
    inc si
    jmp .eat
.next:
    cmp byte [si], 0
    je .ok
    call fs_token_from_si
    cmp byte [fs_token_buf], 0
    je .after
    mov si, fs_token_buf
    mov di, fs_dot
    call string_equals
    cmp al, 1
    je .after
    mov si, fs_token_buf
    mov di, fs_dotdot
    call string_equals
    cmp al, 1
    jne .lookup
    mov al, [fs_node_parent + bx]
    mov bl, al
    jmp .after
.lookup:
    call fs_find_child
    jc .fail
    mov bl, al
.after:
    cmp byte [si], '/'
    jne .next
    inc si
    jmp .eat
.ok:
    mov al, bl
    clc
    pop si
    pop bx
    pop ax
    ret
.fail:
    stc
    pop si
    pop bx
    pop ax
    ret

fs_dot db ".",0
fs_dotdot db "..",0

fs_ls:
    push ax
    push bx
    push si
    mov si, [args_ptr]
    call skip_spaces
    cmp byte [si], 0
    jne .path
    xor bx, bx
    mov bl, [fs_cwd]
    jmp .list
.path:
    mov di, fs_path_buf
    mov cx, 63
.cp:
    mov al, [si]
    cmp al, 0
    je .cpd
    cmp al, ' '
    je .cpd
    stosb
    inc si
    loop .cp
.cpd:
    mov al, 0
    stosb
    mov si, fs_path_buf
    call fs_resolve_path
    jc .fail
    xor bx, bx
    mov bl, al
.list:
    cmp byte [fs_node_type + bx], FS_TYPE_DIR
    jne .fail
    mov dl, [fs_node_first_child + bx]
.iter:
    test dl, dl
    jz .done
    xor bx, bx
    mov bl, dl
    call fs_name_ptr
    mov si, di
    call print_string
    call print_newline
    mov dl, [fs_node_next_sibling + bx]
    jmp .iter
.done:
    clc
    pop si
    pop bx
    pop ax
    ret
.fail:
    stc
    pop si
    pop bx
    pop ax
    ret

fs_cd:
    push bx
    push si
    mov si, [args_ptr]
    call skip_spaces
    cmp byte [si], 0
    je .fail
    mov di, fs_path_buf
    mov cx, 63
.cp:
    mov al, [si]
    cmp al, 0
    je .cpd
    cmp al, ' '
    je .cpd
    stosb
    inc si
    loop .cp
.cpd:
    mov al, 0
    stosb
    mov si, fs_path_buf
    call fs_resolve_path
    jc .fail
    xor bx, bx
    mov bl, al
    cmp byte [fs_node_type + bx], FS_TYPE_DIR
    jne .fail
    mov [fs_cwd], bl
    clc
    pop si
    pop bx
    ret
.fail:
    stc
    pop si
    pop bx
    ret

fs_pwd:
    push ax
    push bx
    push cx
    push di
    push si
    mov bl, [fs_cwd]
    cmp bl, 0
    jne .build
    mov si, slash_msg
    call print_string
    call print_newline
    jmp .done
.build:
    mov di, fs_pwd_stack
    xor cx, cx
.push:
    mov [di], bl
    inc di
    inc cx
    xor bx, bx
    mov bl, [di - 1]
    mov al, [fs_node_parent + bx]
    mov bl, al
    cmp bl, 0
    jne .push
.print:
    dec di
    mov bl, [di]
    mov si, slash_msg
    call print_string
    xor bx, bx
    mov bl, [di]
    call fs_name_ptr
    mov si, di
    call print_string
    loop .print
    call print_newline
.done:
    pop si
    pop di
    pop cx
    pop bx
    pop ax
    ret
