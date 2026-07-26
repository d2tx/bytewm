# bytepass - standalone x86_64 password generator for bytewm
# pure syscalls, gruvbox ANSI colors, reads /dev/urandom
# build: as -o bytepass.o bytepass.s && ld -o bytepass bytepass.o
# usage:  bytepass [length] [-s]

.section .rodata

# ── file paths ─────────────────────────────────────────
path_urandom:    .asciz  "/dev/urandom"
path_xclip:      .asciz  "/usr/bin/xclip"

# ── xclip argv array (pointers followed by strings) ────
xclip_argv:
    .quad xclip_name
    .quad xclip_sel
    .quad xclip_clip
    .quad xclip_in
    .quad 0
xclip_name:      .asciz  "xclip"
xclip_sel:       .asciz  "-selection"
xclip_clip:      .asciz  "clipboard"
xclip_in:        .asciz  "-in"

# ── character sets (no ambiguous: lI1O0) ───────────────
charset_alpha:
    .ascii  "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789"
.equ CHARSET_ALPHA_LEN, . - charset_alpha          # 54

    .ascii  "!@#$%^&*()-_=+[]{}|;:',.<>?/`~"
.equ CHARSET_SYM_LEN,  . - charset_alpha - CHARSET_ALPHA_LEN  # 29

.equ CHARSET_FULL_LEN, CHARSET_ALPHA_LEN + CHARSET_SYM_LEN    # 83

# ── ANSI colors (11-byte sequences, no null terminator) ─
clr_orange:      .ascii  "\033[38;5;208m"       # 11
clr_aqua:        .ascii  "\033[38;5;108m"       # 11
clr_gray:        .ascii  "\033[38;5;246m"       # 11
clr_reset:       .asciz  "\033[0m"              #  5 with null

# ── styled labels (color codes baked into asciz) ───────
str_title:       .asciz  "\342\224\200\342\224\200 bytepass \342\224\200\342\224\200"
str_newline:     .asciz  "\n"
str_spaces:      .asciz  "  "

str_len_label:   .asciz  "  \033[38;5;108mlength  \033[38;5;246m"
str_mode_label:  .asciz  "  \033[38;5;108mmode    \033[38;5;246m"
str_mode_alpha:  .asciz  "alphanumeric"
str_mode_full:   .asciz  "alphanumeric + symbols"
str_divider:     .asciz  "  \033[38;5;208m\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\342\224\200\033[0m"

.section .data
gres:            .quad   0
envp_save:       .quad   0

.section .bss
randbuf:         .fill   1024, 1, 0
outbuf:          .fill   1024, 1, 0
itobuf:          .fill   24,   1, 0

.equ DEFAULT_LENGTH,  32
.equ MAX_LENGTH,       1023

.section .text
.globl _start

# ═══════════════════════════════════════════════════════
#  OUTPUT HELPERS
# ═══════════════════════════════════════════════════════

# print_raw(%rdi = fd, %rsi = ptr, %rdx = len)
print_raw:
    movq    $1, %rax               # sys_write
    syscall
    movq    %rax, gres(%rip)
    ret

# _strlen(%rsi = str) → %rax
_strlen:
    xorq    %rcx, %rcx
    decq    %rcx
1:  incq    %rcx
    cmpb    $0, (%rsi, %rcx)
    jne     1b
    movq    %rcx, %rax
    ret

# printz(%rdi = fd, %rsi = null-terminated str)
printz:
    pushq   %rsi
    call    _strlen
    movq    %rax, %rdx
    popq    %rsi
    call    print_raw
    ret

# printz_ln(%rdi = fd, %rsi = str) — prints then newline
printz_ln:
    call    printz
    movq    $1, %rdi
    leaq    str_newline(%rip), %rsi
    call    printz
    ret

# print_labeled(%rdi = fd, %rsi = label, %rdx = value)
print_labeled:
    pushq   %r12
    pushq   %r13
    movq    %rdx, %r12
    movq    %rsi, %r13
    movq    $1, %rdi
    call    printz
    movq    %r12, %rsi
    call    printz_ln
    popq    %r13
    popq    %r12
    ret

# itoa(%rax = number, %rsi = dest buffer) — needs 24 bytes
itoa:
    pushq   %r12
    movq    %rax, %r12
    movq    %rsi, %rdi
    addq    $23, %rdi
    movb    $0, (%rdi)
    decq    %rdi
    testq   %r12, %r12
    jnz     itoa_loop
    movb    $'0', (%rdi)
    decq    %rdi
    jmp     itoa_done
itoa_loop:
    movq    %r12, %rax
    xorq    %rdx, %rdx
    movq    $10, %rcx
    divq    %rcx
    movq    %rax, %r12
    addb    $'0', %dl
    movb    %dl, (%rdi)
    decq    %rdi
    testq   %r12, %r12
    jnz     itoa_loop
itoa_done:
    incq    %rdi
    movq    %rsi, %r12
itoa_copy:
    movb    (%rdi), %al
    movb    %al, (%r12)
    testb   %al, %al
    jz      itoa_ret
    incq    %rdi
    incq    %r12
    jmp     itoa_copy
itoa_ret:
    popq    %r12
    ret

# ═══════════════════════════════════════════════════════
#  SIMPLE atoi   (%rsi = str) → %rax
# ═══════════════════════════════════════════════════════

atoi:
    xorq    %rax, %rax
    xorq    %rcx, %rcx
at_loop:
    movb    (%rsi, %rcx), %dl
    testb   %dl, %dl
    jz      at_done
    cmpb    $'0', %dl
    jb      at_fail
    cmpb    $'9', %dl
    ja      at_fail
    imulq   $10, %rax
    subb    $'0', %dl
    movzbq  %dl, %rdx
    addq    %rdx, %rax
    incq    %rcx
    jmp     at_loop
at_fail:
    xorq    %rax, %rax
at_done:
    ret

# ═══════════════════════════════════════════════════════
#  MAIN
# ═══════════════════════════════════════════════════════

_start:
    # ── save environ pointer ──
    movq    (%rsp), %rax            # argc
    leaq    16(%rsp, %rax, 8), %r9  # envp (skips argc + argv + sentinel NULL)
    movq    %r9, envp_save(%rip)

    # ── parse command-line ──
    # stack: [argc][argv[0]][argv[1]]...
    movq    %rax, %r12              # argc
    movq    $DEFAULT_LENGTH, %r13   # length
    xorq    %r14, %r14              # sym flag

    cmpq    $1, %r12
    jbe     parse_done

    movq    $1, %r15                # arg index
parse_loop:
    cmpq    %r12, %r15
    jae     parse_done
    movq    8(%rsp, %r15, 8), %rsi   # argv[i]

    cmpb    $'-', (%rsi)
    jne     try_number

    cmpb    $'s', 1(%rsi)
    jne     1f
    movq    $1, %r14
    jmp     parse_next
1:
    cmpb    $'-', 1(%rsi)
    jne     parse_next
    cmpl    $0x626D7973, 2(%rsi)     # "symb"
    jne     parse_next
    cmpb    $'o', 6(%rsi)
    jne     parse_next
    cmpb    $'l', 7(%rsi)
    jne     parse_next
    cmpb    $'s', 8(%rsi)
    jne     parse_next
    cmpb    $0,   9(%rsi)
    jne     parse_next
    movq    $1, %r14
    jmp     parse_next

try_number:
    cmpb    $'0', (%rsi)
    jb      parse_next
    cmpb    $'9', (%rsi)
    ja      parse_next
    call    atoi
    cmpq    $0, %rax
    je      parse_next
    cmpq    $MAX_LENGTH, %rax
    jbe     1f
    movq    $MAX_LENGTH, %rax
1:  movq    %rax, %r13

parse_next:
    incq    %r15
    jmp     parse_loop

parse_done:

    # ── header ──
    leaq    clr_orange(%rip), %rsi
    movq    $11, %rdx
    movq    $1, %rdi
    call    print_raw
    leaq    str_title(%rip), %rsi
    movq    $1, %rdi
    call    printz
    leaq    clr_reset(%rip), %rsi
    call    printz_ln

    # ── length ──
    movq    %r13, %rax
    leaq    itobuf(%rip), %rsi
    call    itoa
    movq    $1, %rdi
    leaq    str_len_label(%rip), %rsi
    leaq    itobuf(%rip), %rdx
    call    print_labeled

    # ── mode ──
    movq    $1, %rdi
    leaq    str_mode_label(%rip), %rsi
    leaq    str_mode_alpha(%rip), %rdx
    cmpq    $0, %r14
    je      1f
    leaq    str_mode_full(%rip), %rdx
1:  call    print_labeled

    # ── divider ──
    movq    $1, %rdi
    leaq    str_divider(%rip), %rsi
    call    printz_ln

    # ── open /dev/urandom ──
    movq    $2, %rax               # sys_open
    leaq    path_urandom(%rip), %rdi
    xorq    %rsi, %rsi             # O_RDONLY
    xorq    %rdx, %rdx
    syscall
    testq   %rax, %rax
    js      exit_fail

    movq    %rax, %r12             # fd

    # ── read random bytes ──
    movq    $0, %rax               # sys_read
    movq    %r12, %rdi
    leaq    randbuf(%rip), %rsi
    movq    %r13, %rdx             # length
    syscall
    cmpq    %r13, %rax
    jne     exit_fail

    # close fd
    movq    $3, %rax
    movq    %r12, %rdi
    syscall

    # ── map bytes to charset ──
    leaq    charset_alpha(%rip), %r12  # charset ptr
    movq    $CHARSET_ALPHA_LEN, %r15   # charset len
    cmpq    $0, %r14
    je      1f
    movq    $CHARSET_FULL_LEN, %r15
1:

    leaq    outbuf(%rip), %rdi
    leaq    randbuf(%rip), %r8
    xorq    %rcx, %rcx

map_loop:
    cmpq    %r13, %rcx
    jae     map_done

    movzbq  (%r8, %rcx), %rax

    xorq    %rdx, %rdx
    divq    %r15                  # rdx = byte % charset_len
    movq    %rdx, %rax

    movb    (%r12, %rax), %al
    movb    %al, (%rdi, %rcx)

    incq    %rcx
    jmp     map_loop

map_done:
    movb    $0, (%rdi, %rcx)

    # ── copy password to clipboard via xclip ──
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15

    movq    $22, %rax              # sys_pipe
    subq    $16, %rsp              # pipefd[2]
    movq    %rsp, %rdi
    syscall
    testq   %rax, %rax
    js      clip_skip_stack

    movl    (%rsp), %r12d          # read end
    movl    4(%rsp), %r13d         # write end

    movq    $57, %rax              # sys_fork
    syscall
    testq   %rax, %rax
    js      clip_close_both
    jz      clip_child

    # parent: close read end, write password, close write end
    movq    $3, %rax
    movl    %r12d, %edi
    syscall

    leaq    outbuf(%rip), %rsi
    call    _strlen
    movq    %rax, %rdx
    movq    $1, %rax
    movl    %r13d, %edi
    syscall

    movq    $3, %rax
    movl    %r13d, %edi
    syscall
    addq    $16, %rsp
    jmp     clip_done

clip_close_both:
    movq    $3, %rax
    movl    %r12d, %edi
    syscall
    movq    $3, %rax
    movl    %r13d, %edi
    syscall
clip_skip_stack:
    addq    $16, %rsp
    jmp     clip_done

clip_child:
    movq    $3, %rax
    movl    %r13d, %edi
    syscall

    movq    $33, %rax              # sys_dup2
    movl    %r12d, %edi
    xorq    %rsi, %rsi             # STDIN
    syscall

    movq    $3, %rax
    movl    %r12d, %edi
    syscall

    movq    $59, %rax              # sys_execve
    leaq    path_xclip(%rip), %rdi
    leaq    xclip_argv(%rip), %rsi
    movq    envp_save(%rip), %rdx
    syscall

    movq    $60, %rax              # execve failed
    movq    $1, %rdi
    syscall

clip_done:
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12

    # ── print password ──
    # "  " in gray
    movq    $1, %rdi
    leaq    str_spaces(%rip), %rsi
    call    printz

    # password in aqua
    movq    $11, %rdx
    movq    $1, %rdi
    leaq    clr_aqua(%rip), %rsi
    call    print_raw

    movq    $1, %rdi
    leaq    outbuf(%rip), %rsi
    call    printz

    # reset + newline
    leaq    clr_reset(%rip), %rsi
    call    printz_ln
    leaq    str_newline(%rip), %rsi
    call    printz

    # ── exit ──
    movq    $60, %rax
    xorq    %rdi, %rdi
    syscall

exit_fail:
    movq    $60, %rax
    movq    $1, %rdi
    syscall
