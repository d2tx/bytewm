# bytekill - standalone x86_64 process killer for bytewm
# pure syscalls, gruvbox ANSI colors
# build: as -o bytekill.o bytekill.s && ld -o bytekill bytekill.o
# usage:  bytekill -l              list processes
#         bytekill [-9] <name>     kill by name (SIGTERM default)
#         bytekill [-9] <pid>      kill by PID

.section .rodata
path_proc:     .asciz  "/proc/"
path_comm:     .asciz  "/comm"
str_newline:   .asciz  "\n"
str_space:     .asciz  " "
str_slash:     .asciz  "  "

# ANSI colors
clr_aqua:      .ascii  "\033[38;5;108m"
clr_gray:      .ascii  "\033[38;5;246m"
clr_red:       .ascii  "\033[38;5;124m"
clr_orange:    .ascii  "\033[38;5;208m"
clr_reset:     .asciz  "\033[0m"
clr_nl:        .asciz  "\n\033[0m"

.equ CLR_AQUA_LEN,   11
.equ CLR_GRAY_LEN,   11
.equ CLR_RED_LEN,    11
.equ CLR_ORANGE_LEN, 11
.equ CLR_RESET_LEN,   4

str_title:     .asciz  "\342\224\200\342\224\200 bytekill \342\224\200\342\224\200"
str_killed:    .asciz  "killed "
str_pping:     .asciz  " ("
str_ppid:      .asciz  ")"
str_none:      .asciz  "no processes matched.\n"
str_usage:     .asciz  "usage: bytekill [-9] [-l] <name|pid>\n"

MAX_PID = 65536

.section .data
gres:          .quad   0

.section .bss
commbuf:       .fill   256, 1, 0
pathbuf:       .fill   128, 1, 0
itobuf:        .fill   24,  1, 0

.section .text
.globl _start

# ═══════════════════════════════════════════════════════
#  HELPERS
# ═══════════════════════════════════════════════════════

print_raw:
    movq    $1, %rax
    syscall
    movq    %rax, gres(%rip)
    ret

_strlen:
    xorq    %rcx, %rcx
    decq    %rcx
1:  incq    %rcx
    cmpb    $0, (%rsi, %rcx)
    jne     1b
    movq    %rcx, %rax
    ret

printz:
    pushq   %rsi
    call    _strlen
    movq    %rax, %rdx
    popq    %rsi
    movq    $1, %rax
    movq    $1, %rdi
    syscall
    ret

printz_ln:
    call    printz
    leaq    str_newline(%rip), %rsi
    call    printz
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

# atoi(%rsi = str) → %rax (0 on failure)
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

# str_eq(%rsi = a, %rdi = b) → %rax = 1 if equal
str_eq:
    pushq   %r12
    pushq   %rbx
    movq    %rsi, %r12
    movq    %rdi, %rbx
1:  movb    (%r12), %al
    cmpb    (%rbx), %al
    jne     se_no
    testb   %al, %al
    jz      se_yes
    incq    %r12
    incq    %rbx
    jmp     1b
se_yes:
    movq    $1, %rax
    jmp     se_done
se_no:
    xorq    %rax, %rax
se_done:
    popq    %rbx
    popq    %r12
    ret

# read_comm(%rdi = pid) → %rax = bytes read, writes to commbuf
read_comm:
    pushq   %rbx
    movq    %rdi, %rbx

    # build path: /proc/PID/comm
    leaq    pathbuf(%rip), %rdi
    leaq    path_proc(%rip), %rsi
    # copy "/proc/" (6 bytes)
    movl    $0x6f72702f, (%rdi)   # "/pro"
    movw    $0x2f63, 4(%rdi)       # "c/"
    leaq    6(%rdi), %rsi
    movq    %rbx, %rax
    call    itoa                    # PID digits at rsi
    leaq    pathbuf+6(%rip), %rsi
    call    _strlen                # rax = PID digit count
    leaq    pathbuf+6(%rip), %rdi
    addq    %rax, %rdi
    # copy "/comm" (5 bytes)
    movl    $0x6d6f632f, (%rdi)     # "/com"
    movb    $0x6d, 4(%rdi)          # "m"
    movb    $0,   5(%rdi)

    # open /proc/PID/comm
    movq    $2, %rax               # sys_open
    leaq    pathbuf(%rip), %rdi
    xorq    %rsi, %rsi             # O_RDONLY
    xorq    %rdx, %rdx
    syscall
    testq   %rax, %rax
    js      rc_fail

    movq    %rax, %rdi             # fd
    movq    $0, %rax               # sys_read
    leaq    commbuf(%rip), %rsi
    movq    $255, %rdx
    syscall
    pushq   %rax

    # strip trailing newline
    leaq    commbuf(%rip), %rdi
    decq    %rax
    cmpb    $'\n', (%rdi, %rax)
    jne     1f
    movb    $0, (%rdi, %rax)
1:
    movq    $3, %rax               # sys_close
    syscall

    popq    %rax                   # bytes read
    jmp     rc_done
rc_fail:
    xorq    %rax, %rax
rc_done:
    popq    %rbx
    ret

# kill_by_name(%r12 = name, %r13 = signal)
kill_by_name:
    pushq   %r14
    xorq    %r14, %r14             # kill count
    movq    $1, %rbx               # pid

kbn_loop:
    cmpq    $MAX_PID, %rbx
    jae     kbn_done
    movq    %rbx, %rdi
    call    read_comm
    cmpq    $0, %rax
    jle     kbn_next

    # null-terminate commbuf
    leaq    commbuf(%rip), %rdi
    movb    $0, (%rdi, %rax)

    # compare with target name
    leaq    commbuf(%rip), %rsi
    movq    %r12, %rdi
    call    str_eq
    cmpq    $1, %rax
    jne     kbn_next

    # match: kill it
    movq    $62, %rax              # sys_kill
    movq    %rbx, %rdi             # pid
    movq    %r13, %rsi             # sig
    syscall
    incq    %r14
kbn_next:
    incq    %rbx
    jmp     kbn_loop
kbn_done:
    movq    %r14, %rax
    popq    %r14
    ret

# list_processes — scan /proc, print PID + name
list_procs:
    pushq   %rbx
    movq    $1, %rbx               # pid
lp_loop:
    cmpq    $MAX_PID, %rbx
    jae     lp_done
    movq    %rbx, %rdi
    call    read_comm
    cmpq    $0, %rax
    jle     lp_next

    leaq    commbuf(%rip), %rdi
    movb    $0, (%rdi, %rax)

    # print PID in aqua
    leaq    clr_aqua(%rip), %rsi
    movq    $CLR_AQUA_LEN, %rdx
    movq    $1, %rdi
    call    print_raw

    movq    %rbx, %rax
    leaq    itobuf(%rip), %rsi
    call    itoa
    leaq    itobuf(%rip), %rsi
    call    printz

    # print space
    leaq    str_space(%rip), %rsi
    call    printz

    # print comm in gray
    leaq    clr_gray(%rip), %rsi
    movq    $CLR_GRAY_LEN, %rdx
    movq    $1, %rdi
    call    print_raw

    leaq    commbuf(%rip), %rsi
    call    printz

    # reset + newline
    leaq    clr_reset(%rip), %rsi
    call    printz_ln

lp_next:
    incq    %rbx
    jmp     lp_loop
lp_done:
    popq    %rbx
    ret

# ═══════════════════════════════════════════════════════
#  MAIN
# ═══════════════════════════════════════════════════════

_start:
    movq    (%rsp), %r12            # argc
    movq    $15, %r13               # default signal = SIGTERM
    movq    $0, %r14                # target name ptr (NULL = no target)
    movq    $0, %r15                # target pid (0 = no target)
    cmpq    $2, %r12
    jb      usage

    movq    $1, %rbx                # arg index
parse_args:
    cmpq    %r12, %rbx
    jae     parse_done
    movq    8(%rsp, %rbx, 8), %rsi  # argv[i]

    cmpb    $'-', (%rsi)
    jne     pa_notflag

    cmpb    $'9', 1(%rsi)
    jne     pa_check_l
    movq    $9, %r13               # SIGKILL
    jmp     pa_next

pa_check_l:
    cmpb    $'l', 1(%rsi)
    jne     pa_next
    movq    $MAX_PID, %r15         # sentinel for list mode
    jmp     pa_next

pa_notflag:
    # is it a number?
    cmpb    $'0', (%rsi)
    jb      pa_name
    cmpb    $'9', (%rsi)
    ja      pa_name
    call    atoi
    cmpq    $0, %rax
    je      pa_name
    cmpq    $MAX_PID, %rax
    ja      pa_name
    movq    %rax, %r15             # target PID
    jmp     pa_next

pa_name:
    movq    %rsi, %r14             # target name
    jmp     pa_next

pa_next:
    incq    %rbx
    jmp     parse_args

parse_done:

    # ── list mode ──
    movq    %r15, %rax
    cmpq    $MAX_PID, %rax
    jae     do_list

    # ── header ──
    leaq    clr_orange(%rip), %rsi
    movq    $CLR_ORANGE_LEN, %rdx
    movq    $1, %rdi
    call    print_raw
    leaq    str_title(%rip), %rsi
    call    printz
    leaq    clr_reset(%rip), %rsi
    call    printz_ln

    # ── kill by PID ──
    cmpq    $0, %r15
    je      kill_by_nam

    movq    $62, %rax              # sys_kill
    movq    %r15, %rdi
    movq    %r13, %rsi
    syscall
    testq   %rax, %rax
    js      exit_fail

    # show killed message
    leaq    clr_red(%rip), %rsi
    movq    $CLR_RED_LEN, %rdx
    movq    $1, %rdi
    call    print_raw
    leaq    str_killed(%rip), %rsi
    call    printz
    movq    %r15, %rax
    leaq    itobuf(%rip), %rsi
    call    itoa
    leaq    itobuf(%rip), %rsi
    call    printz
    leaq    clr_reset(%rip), %rsi
    call    printz_ln
    jmp     exit_ok

kill_by_nam:
    cmpq    $0, %r14
    je      usage

    # kill by name
    movq    %r14, %r12
    movq    %r13, %rsi
    call    kill_by_name            # returns kill count in rax

    cmpq    $0, %rax
    je      no_match

    # print count
    leaq    clr_red(%rip), %rsi
    movq    $CLR_RED_LEN, %rdx
    movq    $1, %rdi
    call    print_raw
    leaq    str_killed(%rip), %rsi
    call    printz
    movq    %rax, %rax              # use count in rax
    leaq    itobuf(%rip), %rsi
    call    itoa
    leaq    itobuf(%rip), %rsi
    call    printz
    leaq    str_pping(%rip), %rsi
    call    printz
    movq    %r12, %rsi
    call    printz
    leaq    str_ppid(%rip), %rsi
    call    printz
    leaq    clr_reset(%rip), %rsi
    call    printz_ln
    jmp     exit_ok

no_match:
    leaq    clr_gray(%rip), %rsi
    movq    $CLR_GRAY_LEN, %rdx
    movq    $1, %rdi
    call    print_raw
    leaq    str_none(%rip), %rsi
    call    printz
    jmp     exit_ok

do_list:
    # header
    leaq    clr_orange(%rip), %rsi
    movq    $CLR_ORANGE_LEN, %rdx
    movq    $1, %rdi
    call    print_raw
    leaq    str_title(%rip), %rsi
    call    printz
    leaq    clr_reset(%rip), %rsi
    call    printz_ln

    call    list_procs
    jmp     exit_ok

usage:
    leaq    str_usage(%rip), %rsi
    call    printz

exit_ok:
    movq    $60, %rax
    xorq    %rdi, %rdi
    syscall

exit_fail:
    movq    $60, %rax
    movq    $1, %rdi
    syscall
