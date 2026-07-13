# bytefetch - minimal assembly system info for bytewm
# x86_64 Linux, pure syscalls, gruvbox ANSI colors
# build: as -o bytefetch.o bytefetch.s && ld -o bytefetch bytefetch.o

.section .rodata

# ── file paths ─────────────────────────────────────────
path_osrel:    .asciz  "/etc/os-release"
path_cpuinfo:  .asciz  "/proc/cpuinfo"
path_meminfo:  .asciz  "/proc/meminfo"
path_uptime:   .asciz  "/proc/uptime"

# ── search keys ────────────────────────────────────────
key_pretty:    .asciz  "PRETTY_NAME=\""
key_cpu:       .asciz  "model name\t: "

# ── ANSI colors (256-color palette) ───────────────────
clr_reset:     .asciz  "\033[0m"
clr_orange:    .ascii  "\033[38;5;208m"       # no null
clr_aqua:      .ascii  "\033[38;5;108m"
clr_gray:      .ascii  "\033[38;5;246m"
clr_light:     .ascii  "\033[38;5;223m"
clr_red:       .ascii  "\033[38;5;124m"
clr_yellow:    .ascii  "\033[38;5;178m"
clr_end:                                     # sentinel

# ── string constants ───────────────────────────────────
str_title:     .asciz  "\342\224\200\342\224\200 bytefetch \342\224\200\342\224\200"
str_sep:       .asciz  "  "
str_newline:   .asciz  "\n"
str_space:     .asciz  "  "
str_lparen:    .asciz  "("
str_rparen:    .asciz  ")"
str_at:        .asciz  "@"
str_kb:        .asciz  " MB"
str_mb:        .asciz  " GB"
str_fallback:  .asciz  "Linux"

# ── label strings ──────────────────────────────────────
lbl_wm:        .asciz  "  \033[38;5;108mwm      \033[38;5;246m"
lbl_os:        .asciz  "  \033[38;5;108mos      \033[38;5;246m"
lbl_kernel:    .asciz  "  \033[38;5;108mkernel  \033[38;5;246m"
lbl_uptime:    .asciz  "  \033[38;5;108muptime  \033[38;5;246m"
lbl_cpu:       .asciz  "  \033[38;5;108mcpu     \033[38;5;246m"
lbl_mem:       .asciz  "  \033[38;5;108mmem     \033[38;5;246m"

.section .data
gres:          .quad   0

.section .bss

uts:           .fill   390, 1, 0           # struct utsname

buf_osrel:     .fill   4096, 1, 0
buf_cpuinfo:   .fill   4096, 1, 0
buf_meminfo:   .fill   4096, 1, 0
buf_uptime:    .fill   256, 1, 0

out_host:      .fill   65, 1, 0
out_kernel:    .fill   65, 1, 0
out_cpu:       .fill   128, 1, 0
out_wm:        .fill   32, 1, 0
out_os:        .fill   128, 1, 0
out_uptime:    .fill   32, 1, 0
out_mem:       .fill   64, 1, 0
out_tmp:       .fill   64, 1, 0

tmpstr:        .fill   32, 1, 0
itobuf:        .fill   24, 1, 0
path_comm:     .fill   32, 1, 0

.section .text
.globl _start

# ═══════════════════════════════════════════════════════
#  OUTPUT HELPERS
# ═══════════════════════════════════════════════════════

# print_string(%rdi = fd, %rsi = ptr, %rdx = len)
# returns bytes written in gres
print_raw:
    movq    $1, %rax                # sys_write
    syscall
    movq    %rax, gres(%rip)
    ret

# strlen_inline(%rsi = str) -> %rax = length
_strlen:
    xorq    %rcx, %rcx
    decq    %rcx
1:  incq    %rcx
    cmpb    $0, (%rsi, %rcx)
    jne     1b
    movq    %rcx, %rax
    ret

# printz(%rdi = fd, %rsi = null-terminated string)
printz:
    pushq   %rsi
    call    _strlen
    movq    %rax, %rdx
    popq    %rsi
    call    print_raw
    ret

# printz_ln(%rdi = fd, %rsi = str)
printz_ln:
    call    printz
    movq    $1, %rdi
    leaq    str_newline(%rip), %rsi
    call    printz
    ret

# print_title(%rdi = fd)
print_title:
    pushq   %rdi
    # orange "── bytefetch ──"
    leaq    clr_orange(%rip), %rsi
    movq    $11, %rdx
    movq    $1, %rdi
    call    print_raw
    leaq    str_title(%rip), %rsi
    call    printz
    # fall through to reset

    leaq    clr_reset(%rip), %rsi
    movq    $4, %rdx
    movq    $1, %rdi
    call    print_raw
    leaq    str_newline(%rip), %rsi
    movq    $1, %rdi
    call    printz_ln
    popq    %rdi
    ret

# print_labeled(%rdi = fd, %rsi = label_str, %rdx = value_ptr)
print_labeled:
    pushq   %r12
    pushq   %r13
    movq    %rdx, %r12             # value ptr
    movq    %rsi, %r13             # label str

    movq    $1, %rdi
    movq    %r13, %rsi
    call    printz                  # print label with embedded colors

    movq    %r12, %rsi
    call    printz_ln               # print value + newline + reset

    popq    %r13
    popq    %r12
    ret

# print_hostline(%rdi = fd)
# prints: "  user@host"
print_hostline:
    pushq   %r12
    # aqua hostname
    leaq    clr_aqua(%rip), %rsi
    movq    $11, %rdx
    movq    $1, %rdi
    call    print_raw
    leaq    out_host(%rip), %rsi
    movq    $1, %rdi
    call    printz

    # gray @
    leaq    clr_gray(%rip), %rsi
    movq    $11, %rdx
    movq    $1, %rdi
    call    print_raw
    leaq    str_at(%rip), %rsi
    movq    $1, %rdi
    call    printz

    # aqua OS name
    leaq    clr_aqua(%rip), %rsi
    movq    $11, %rdx
    movq    $1, %rdi
    call    print_raw
    leaq    out_os(%rip), %rsi
    movq    $1, %rdi
    call    printz_ln

    # reset then newline
    leaq    str_newline(%rip), %rsi
    movq    $1, %rdi
    call    printz

    popq    %r12
    ret

# ═══════════════════════════════════════════════════════
#  FILE I/O
# ═══════════════════════════════════════════════════════

# read_file(%rsi = path, %rdx = buffer, %rcx = maxsize)
# returns bytes read in %rax, or -1 on error
# clobbers: rax, rdi, rsi, rdx (standard syscall)
read_file:
    pushq   %r12
    pushq   %rbx
    pushq   %rcx                   # save maxsize (rcx clobbered by syscall)
    movq    %rdx, %r12             # save buffer

    # open(path, O_RDONLY)
    movq    $2, %rax
    xorq    %rdi, %rdi
    movq    %rsi, %rdi             # path
    xorq    %rsi, %rsi             # O_RDONLY = 0
    xorq    %rdx, %rdx
    syscall
    cmpq    $0, %rax
    jl      rf_err_pop
    movq    %rax, %rbx             # fd

    # read(fd, buf, maxsize)
    movq    $0, %rax
    movq    %rbx, %rdi
    movq    %r12, %rsi
    popq    %rdx                   # restore maxsize
    syscall
    pushq   %rax                   # save bytes read

    # close(fd)
    movq    $3, %rax
    movq    %rbx, %rdi
    syscall

    popq    %rax                    # restore bytes read
    popq    %rbx
    popq    %r12
    ret
rf_err_pop:
    popq    %rcx                    # clean maxsize off stack
rf_err_nopop:
    movq    $-1, %rax
    popq    %rbx
    popq    %r12
    ret

# ═══════════════════════════════════════════════════════
#  STRING PARSING
# ═══════════════════════════════════════════════════════

# find_after(%rsi = haystack, %rdx = haylen, %rdi = needle)
# needle must be null-terminated
# returns pointer to first char after needle in %rax, or NULL
find_after:
    pushq   %r12
    pushq   %r13
    pushq   %r14
    movq    %rdi, %r12             # needle
    movq    %rsi, %r13             # haystack
    movq    %rdx, %r14             # haylen
    movq    %r12, %rsi
    call    _strlen                # needle len -> %rax
    movq    %rax, %r12             # needle len

    xorq    %rcx, %rcx             # hay index
fa_loop:
    movq    %r14, %rax
    # r14 may be negative, skip check if so
    testq   %r14, %r14
    js      1f
    cmpq    %r14, %rcx
    jae     fa_notfound
1:
    movq    %r13, %rsi
    addq    %rcx, %rsi             # hay + idx
    movq    %rdi, %r8              # remember start for later

    # compare needle_len chars
    xorq    %rax, %rax             # inner counter
fa_cmp:
    cmpq    %r12, %rax
    jae     fa_found               # matched full needle
    movb    (%rsi, %rax), %r9b
    cmpb    (%rdi, %rax), %r9b
    jne     fa_next
    incq    %rax
    jmp     fa_cmp
fa_next:
    incq    %rcx
    jmp     fa_loop
fa_found:
    leaq    (%r13, %rcx), %rax
    addq    %r12, %rax             # pointer after needle
    jmp     fa_done
fa_notfound:
    xorq    %rax, %rax
fa_done:
    popq    %r14
    popq    %r13
    popq    %r12
    ret

# copy_until(%rsi = src, %rdi = dst, %dl = stop_char, %cl = maxlen)
# returns bytes copied in %rax
# NULL-terminates destination
copy_until:
    pushq   %r12
    movq    %rdi, %r12             # dst
    movb    %dl, %r10b             # stop char
    xorq    %rax, %rax             # count
    movzbl  %cl, %ecx              # maxlen
    decq    %rcx                    # leave room for null
cu_loop:
    cmpq    %rcx, %rax
    jae     cu_term
    movb    (%rsi, %rax), %r9b
    testb   %r9b, %r9b
    jz      cu_term
    cmpb    %r10b, %r9b
    je      cu_term
    movb    %r9b, (%r12, %rax)
    incq    %rax
    jmp     cu_loop
cu_term:
    movb    $0, (%r12, %rax)
    popq    %r12
    ret

# copy_until_strict(%rsi = src, %rdi = dst, %dl = stop_char, %cl = maxlen)
# like copy_until but stops ONLY at stop_char or maxlen (not null)
copy_until_strict:
    pushq   %r12
    movq    %rdi, %r12             # dst
    movb    %dl, %r10b             # stop char
    xorq    %rax, %rax             # count
    movzbl  %cl, %ecx              # maxlen
    decq    %rcx
cus_loop:
    cmpq    %rcx, %rax
    jae     cus_term
    movb    (%rsi, %rax), %r9b
    cmpb    %r10b, %r9b
    je      cus_term
    movb    %r9b, (%r12, %rax)
    incq    %rax
    jmp     cus_loop
cus_term:
    movb    $0, (%r12, %rax)
    popq    %r12
    ret

# ═══════════════════════════════════════════════════════
#  NUMBER FORMATTING
# ═══════════════════════════════════════════════════════

# itoa(%rax = number, %rsi = dest buffer)
# dest must be at least 24 bytes
itoa:
    pushq   %r12
    movq    %rax, %r12             # number
    movq    %rsi, %rdi             # dest
    addq    $23, %rdi              # write backwards
    movb    $0, (%rdi)             # null terminator
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
    movq    %rax, %r12             # quotient
    addb    $'0', %dl
    movb    %dl, (%rdi)
    decq    %rdi
    testq   %r12, %r12
    jnz     itoa_loop

itoa_done:
    incq    %rdi                    # point to first digit
    # copy to start of buffer
    movq    %rsi, %r12             # original dest start
    movq    %rdi, %rsi             # source
itoa_copy:
    movb    (%rsi), %al
    movb    %al, (%r12)
    testb   %al, %al
    jz      itoa_ret
    incq    %rsi
    incq    %r12
    jmp     itoa_copy
itoa_ret:
    popq    %r12
    ret

# format_uptime(%rax = float as integer seconds, %rdi = dest)
# reads float from uptime buffer, converts seconds to "Xd Xh Xm" or "Xh Xm" or "Xm"
# format_uptime(%rax = float as integer seconds, %rdi = dest)
# reads float from uptime buffer, converts seconds to "Xd Xh Xm" or "Xh Xm" or "Xm"
fmt_uptime:
    pushq   %r12
    pushq   %r13
    pushq   %rbx
    movq    %rdi, %rbx             # dest (rbx survives calls)

    # parse integer part of uptime float (before decimal)
    leaq    buf_uptime(%rip), %rsi
    xorq    %rax, %rax             # seconds accumulator
    xorq    %rcx, %rcx
fu_parse:
    movb    (%rsi, %rcx), %dl
    cmpb    $'.', %dl
    je      fu_parsed
    testb   %dl, %dl
    jz      fu_parsed
    cmpb    $'\n', %dl
    je      fu_parsed
    subb    $'0', %dl
    jb      fu_parsed
    cmpb    $9, %dl
    ja      fu_parsed
    imulq   $10, %rax
    movzbq  %dl, %rdx
    addq    %rdx, %rax
    incq    %rcx
    jmp     fu_parse
fu_parsed:
    movq    %rax, %r12             # total seconds
    movq    %rbx, %rdi             # restore dest ptr

    # days = sec / 86400
    xorq    %rdx, %rdx
    movq    $86400, %rcx
    divq    %rcx
    movq    %rdx, %r12             # remaining seconds
    testq   %rax, %rax
    jz      fu_hours

    # write day number
    pushq   %rdi                    # save dest before call
    leaq    itobuf(%rip), %rsi
    call    itoa
    popq    %rdi                    # restore dest
    leaq    itobuf(%rip), %rsi
1:  movb    (%rsi), %al
    movb    %al, (%rdi)
    testb   %al, %al
    jz      2f
    incq    %rsi
    incq    %rdi
    jmp     1b
2:  movw    $0x2064, (%rdi)        # "d "
    addq    $2, %rdi

fu_hours:
    movq    %r12, %rax             # remaining seconds
    xorq    %rdx, %rdx
    movq    $3600, %rcx
    divq    %rcx
    movq    %rdx, %r12             # remaining seconds
    testq   %rax, %rax
    jz      fu_minutes             # skip 0h if no days shown

    pushq   %rdi                    # save dest
    leaq    itobuf(%rip), %rsi
    call    itoa
    popq    %rdi                    # restore dest
    leaq    itobuf(%rip), %rsi
fu_h_copy:
    movb    (%rsi), %al
    movb    %al, (%rdi)
    testb   %al, %al
    jz      fu_h_done
    incq    %rsi
    incq    %rdi
    jmp     fu_h_copy
fu_h_done:
    movw    $0x2068, (%rdi)        # "h "
    addq    $2, %rdi

fu_minutes:

    # minutes
    movq    %r12, %rax
    xorq    %rdx, %rdx
    movq    $60, %rcx
    divq    %rcx
    pushq   %rdi                    # save dest
    leaq    itobuf(%rip), %rsi
    call    itoa
    popq    %rdi                    # restore dest
    leaq    itobuf(%rip), %rsi
fu_m_copy:
    movb    (%rsi), %al
    movb    %al, (%rdi)
    testb   %al, %al
    jz      fu_m_done
    incq    %rsi
    incq    %rdi
    jmp     fu_m_copy
fu_m_done:
    movw    $0x206d, (%rdi)        # "m"
    incq    %rdi
    movb    $0, (%rdi)

    popq    %rbx
    popq    %r13
    popq    %r12
    ret

# format_mem(%rax = total_kb, %rcx = used_kb)
# writes to out_mem as "XXXX / XXXX MB" or "X.X / X.X GB"
fmt_mem:
    pushq   %r12
    pushq   %r13
    movq    %rax, %r12             # total
    movq    %rcx, %r13             # used

    # if > 2GB (2*1024*1024 KB), show GB
    cmpq    $2097152, %r12
    jb      fm_show_mb

    # convert to GB with 1 decimal: value * 10 / 1024 / 1024 → tenths of GB
    movq    %r12, %rax
    imulq   $10, %rax
    movq    $1048576, %rcx
    xorq    %rdx, %rdx
    divq    %rcx
    movq    %rax, %r12             # total tenths of GB

    movq    %r13, %rax
    imulq   $10, %rax
    movq    $1048576, %rcx
    xorq    %rdx, %rdx
    divq    %rcx
    movq    %rax, %r13             # used tenths of GB

    call    fmt_gb_pair
    jmp     fm_done

fm_show_mb:
    # convert to MB: value / 1024
    movq    %r12, %rax
    xorq    %rdx, %rdx
    movq    $1024, %rcx
    divq    %rcx
    movq    %rax, %r12

    movq    %r13, %rax
    xorq    %rdx, %rdx
    movq    $1024, %rcx
    divq    %rcx
    movq    %rax, %r13

    call    fmt_mb_pair

fm_done:
    popq    %r13
    popq    %r12
    ret

# format a single GB value: r13 tenths → (%rdi) as "X.X"
# advances %rdi past the null
fmt_gb_one:
    movq    %r13, %rax
    xorq    %rdx, %rdx
    movq    $10, %rcx
    divq    %rcx
    pushq   %rdx                   # tenths

    pushq   %rdi
    leaq    itobuf(%rip), %rsi
    call    itoa                    # integer part → itobuf
    popq    %rdi
    leaq    itobuf(%rip), %rsi
1:  movb    (%rsi), %al
    testb   %al, %al
    jz      2f
    movb    %al, (%rdi)
    incq    %rsi
    incq    %rdi
    jmp     1b
2:  movb    $'.', (%rdi)
    incq    %rdi
    popq    %rax
    addb    $'0', %al
    movb    %al, (%rdi)
    incq    %rdi
    movb    $0, (%rdi)
    ret

# write " / " (3 bytes, no null) to (%rdi), advance
fmt_slash:
    movb    $' ', (%rdi)
    incq    %rdi
    movb    $'/', (%rdi)
    incq    %rdi
    movb    $' ', (%rdi)
    incq    %rdi
    ret

# copy %rsi null-terminated string to (%rdi), advance %rdi
fmt_copyz:
1:  movb    (%rsi), %al
    movb    %al, (%rdi)
    testb   %al, %al
    jz      2f
    incq    %rsi
    incq    %rdi
    jmp     1b
2:  ret

# write GB formatted: r13 used tenths, r12 total tenths → out_mem
fmt_gb_pair:
    leaq    out_mem(%rip), %rdi
    movq    %r13, %r8
    movq    %r12, %r9

    movq    %r8, %r13
    call    fmt_gb_one              # used → "X.X\0"

    call    fmt_slash               # " / " overwrites null

    movq    %r9, %r13
    call    fmt_gb_one              # total → "X.X / Y.Y\0"
                                   # rdi is at the null

    leaq    str_mb(%rip), %rsi      # " GB" suffix overwrites null
    call    fmt_copyz
    ret

# write MB formatted: r13 used MB, r12 total MB → out_mem
fmt_mb_pair:
    leaq    out_mem(%rip), %rdi

    movq    %r13, %rax
    leaq    itobuf(%rip), %rsi
    pushq   %rdi
    call    itoa
    popq    %rdi
    leaq    itobuf(%rip), %rsi
    call    fmt_copyz

    call    fmt_slash               # " / " overwrites null

    movq    %r12, %rax
    leaq    itobuf(%rip), %rsi
    pushq   %rdi
    call    itoa
    popq    %rdi
    leaq    itobuf(%rip), %rsi
    call    fmt_copyz               # total → "... / XXXX\0"

    leaq    str_kb(%rip), %rsi
    call    fmt_copyz               # " MB" suffix overwrites null
    ret

# ═══════════════════════════════════════════════════════
#  STRIP QUOTES HELPER
# ═══════════════════════════════════════════════════════

# strip_quotes(%rsi = src, %rdi = dst, %cl = maxlen)
# copies until first '"' is found to end at closing '"'
strip_quotes:
    pushq   %r12
    movq    %rdi, %r12
    xorq    %rax, %rax
    movzbl  %cl, %ecx
    decq    %rcx
sq_loop:
    cmpq    %rcx, %rax
    jae     sq_term
    movb    (%rsi, %rax), %dl
    testb   %dl, %dl
    jz      sq_term
    cmpb    $'"', %dl
    je      sq_term
    movb    %dl, (%r12, %rax)
    incq    %rax
    jmp     sq_loop
sq_term:
    movb    $0, (%r12, %rax)
    popq    %r12
    ret

# ═══════════════════════════════════════════════════════
#  WM DETECTION  (scans /proc for bytewm process)
# ═══════════════════════════════════════════════════════

detect_wm:
    pushq   %r12
    pushq   %r13
    pushq   %rbx

    # PID range: 100 - 65535
    movq    $100, %rbx

dw_scan:
    movq    %rbx, %rax
    # write PID digits to path_comm+6 using itoa
    leaq    path_comm(%rip), %rdi
    movl    $0x6f72702f, (%rdi)     # "/pro"
    movw    $0x2f63, 4(%rdi)        # "c/"
    leaq    6(%rdi), %rsi           # dest for itoa
    call    itoa
    leaq    path_comm+6(%rip), %rsi
    call    _strlen                 # rax = PID string length
    leaq    path_comm+6(%rip), %rdi
    addq    %rax, %rdi              # rdi = after PID digits
    movb    $0x2f, (%rdi)           # "/"
    movl    $0x6d6d6f63, 1(%rdi)    # "comm"
    movb    $0, 5(%rdi)

    # open /proc/PID/comm
    movq    $2, %rax
    leaq    path_comm(%rip), %rdi
    xorq    %rsi, %rsi
    xorq    %rdx, %rdx
    syscall
    cmpq    $0, %rax
    jl      dw_nextpid
    movq    %rax, %r12

    # read comm
    movq    $0, out_tmp(%rip)
    movq    $0, %rax
    movq    %r12, %rdi
    leaq    out_tmp(%rip), %rsi
    movq    $8, %rdx
    syscall

    pushq   %rax
    movq    $3, %rax
    movq    %r12, %rdi
    syscall
    popq    %rax

    # check for "bytewm"
    cmpb    $'b', out_tmp(%rip)
    jne     dw_nextpid
    cmpb    $'y', out_tmp+1(%rip)
    jne     dw_nextpid
    cmpb    $'t', out_tmp+2(%rip)
    jne     dw_nextpid
    cmpb    $'e', out_tmp+3(%rip)
    jne     dw_nextpid
    cmpb    $'w', out_tmp+4(%rip)
    jne     dw_nextpid
    cmpb    $'m', out_tmp+5(%rip)
    jne     dw_nextpid

    # found
    movq    out_tmp(%rip), %r8
    movq    %r8, out_wm(%rip)
    movb    $0, out_wm+6(%rip)
    jmp     dw_done

dw_nextpid:
    incq    %rbx
    cmpq    $65536, %rbx
    jl      dw_scan

dw_done:
    popq    %rbx
    popq    %r13
    popq    %r12
    ret

# ═══════════════════════════════════════════════════════
#  GATHER SYSTEM INFO
# ═══════════════════════════════════════════════════════

gather_sys:
    pushq   %r12

    # ── detect WM by scanning /proc ──
    call    detect_wm

    # ── uname: hostname + kernel ──
    movq    $63, %rax
    leaq    uts(%rip), %rdi
    syscall

    # copy nodename (offset +65 into utsname) -> out_host
    leaq    uts+65(%rip), %rsi
    leaq    out_host(%rip), %rdi
    movb    $0, %dl
    movb    $64, %cl
    call    copy_until

    # copy release (offset +130) -> out_kernel
    leaq    uts+130(%rip), %rsi
    leaq    out_kernel(%rip), %rdi
    movb    $0, %dl
    movb    $64, %cl
    call    copy_until

    # ── OS from /etc/os-release ──
    leaq    path_osrel(%rip), %rsi
    leaq    buf_osrel(%rip), %rdx
    movq    $4096, %rcx
    call    read_file
    cmpq    $0, %rax
    jl      gs_os_fallback
    movq    %rax, %r12             # bytes read

    leaq    buf_osrel(%rip), %rsi
    movq    %r12, %rdx
    leaq    key_pretty(%rip), %rdi
    call    find_after
    testq   %rax, %rax
    jz      gs_os_fallback

    # strip quotes from PRETTY_NAME="..."
    movq    %rax, %rsi
    leaq    out_os(%rip), %rdi
    movb    $127, %cl
    call    strip_quotes
    jmp     gs_os_done

gs_os_fallback:
    leaq    out_os(%rip), %rdi
    leaq    str_fallback(%rip), %rsi
gs_fb_copy:
    movb    (%rsi), %al
    movb    %al, (%rdi)
    testb   %al, %al
    jz      gs_os_done
    incq    %rsi
    incq    %rdi
    jmp     gs_fb_copy
gs_os_done:

    # ── CPU from /proc/cpuinfo ──
    leaq    path_cpuinfo(%rip), %rsi
    leaq    buf_cpuinfo(%rip), %rdx
    movq    $4096, %rcx
    call    read_file
    cmpq    $0, %rax
    jl      gs_cpu_done
    movq    %rax, %r12

    leaq    buf_cpuinfo(%rip), %rsi
    movq    %r12, %rdx
    leaq    key_cpu(%rip), %rdi
    call    find_after
    testq   %rax, %rax
    jz      gs_cpu_done

    movq    %rax, %rsi
    leaq    out_cpu(%rip), %rdi
    movb    $'\n', %dl
    movb    $127, %cl
    call    copy_until_strict
gs_cpu_done:

    # ── Uptime from /proc/uptime ──
    leaq    path_uptime(%rip), %rsi
    leaq    buf_uptime(%rip), %rdx
    movq    $256, %rcx
    call    read_file
    cmpq    $0, %rax
    jl      gs_up_done

    leaq    out_uptime(%rip), %rdi
    call    fmt_uptime
gs_up_done:

    # ── Memory from /proc/meminfo ──
    leaq    path_meminfo(%rip), %rsi
    leaq    buf_meminfo(%rip), %rdx
    movq    $4096, %rcx
    call    read_file
    cmpq    $0, %rax
    jl      gs_mem_skip
    movq    %rax, %r12

    # parse MemTotal and MemAvailable by scanning for keywords directly
    xorq    %r8, %r8               # total kB
    xorq    %r9, %r9               # avail kB
    leaq    buf_meminfo(%rip), %rsi

gs_scan_loop:
    cmpb    $0, (%rsi)
    je      gs_scan_done
    # check for "MemTotal:"
    cmpb    $'M', (%rsi)
    jne     gs_scan_next
    cmpb    $'e', 1(%rsi)
    jne     gs_scan_avail
    cmpb    $'m', 2(%rsi)
    jne     gs_scan_avail
    cmpb    $'T', 3(%rsi)
    jne     gs_scan_avail
    cmpb    $'o', 4(%rsi)
    jne     gs_scan_avail
    cmpb    $'t', 5(%rsi)
    jne     gs_scan_avail
    cmpb    $'a', 6(%rsi)
    jne     gs_scan_avail
    cmpb    $'l', 7(%rsi)
    jne     gs_scan_avail
    cmpb    $':', 8(%rsi)
    jne     gs_scan_avail

    # Found "MemTotal:", skip spaces, parse number
    leaq    9(%rsi), %rsi
    call    gs_parse_number
    movq    %rax, %r8
    jmp     gs_scan_loop

gs_scan_avail:
    cmpb    $'M', (%rsi)
    jne     gs_scan_next
    cmpb    $'e', 1(%rsi)
    jne     gs_scan_next
    cmpb    $'m', 2(%rsi)
    jne     gs_scan_next
    cmpb    $'A', 3(%rsi)
    jne     gs_scan_next
    cmpb    $'v', 4(%rsi)
    jne     gs_scan_next
    cmpb    $'a', 5(%rsi)
    jne     gs_scan_next
    cmpb    $'i', 6(%rsi)
    jne     gs_scan_next
    cmpb    $'l', 7(%rsi)
    jne     gs_scan_next
    cmpb    $'a', 8(%rsi)
    jne     gs_scan_next
    cmpb    $'b', 9(%rsi)
    jne     gs_scan_next
    cmpb    $'l', 10(%rsi)
    jne     gs_scan_next
    cmpb    $'e', 11(%rsi)
    jne     gs_scan_next
    cmpb    $':', 12(%rsi)
    jne     gs_scan_next

    # Found "MemAvailable:", skip spaces, parse number
    leaq    13(%rsi), %rsi
    call    gs_parse_number
    movq    %rax, %r9
    jmp     gs_scan_loop

gs_scan_next:
    incq    %rsi
    jmp     gs_scan_loop

gs_scan_done:
    # if no MemAvailable, use MemFree fallback
    cmpq    $0, %r9
    jne     gs_have_avail
    # try finding MemFree
    leaq    buf_meminfo(%rip), %rsi
gs_scan2:
    cmpb    $0, (%rsi)
    je      gs_have_avail
    cmpb    $'M', (%rsi)
    jne     gs_scan2_next
    cmpb    $'e', 1(%rsi)
    jne     gs_scan2_next
    cmpb    $'m', 2(%rsi)
    jne     gs_scan2_next
    cmpb    $'F', 3(%rsi)
    jne     gs_scan2_next
    cmpb    $'r', 4(%rsi)
    jne     gs_scan2_next
    cmpb    $'e', 5(%rsi)
    jne     gs_scan2_next
    cmpb    $'e', 6(%rsi)
    jne     gs_scan2_next
    cmpb    $':', 7(%rsi)
    jne     gs_scan2_next
    leaq    8(%rsi), %rsi
    call    gs_parse_number
    movq    %rax, %r9
    jmp     gs_have_avail
gs_scan2_next:
    incq    %rsi
    jmp     gs_scan2

gs_have_avail:
    movq    %r8, %rcx
    subq    %r9, %rcx              # used = total - avail
    movq    %r8, %rax              # total kB

    call    fmt_mem
    jmp     gs_mem_end

# number parser: (%rsi) → spaces → digits → number in %rax
# advances %rsi past parsed number
gs_parse_number:
    xorq    %rax, %rax
1:  movb    (%rsi), %dl
    cmpb    $' ', %dl
    jne     2f
    incq    %rsi
    jmp     1b
2:  movb    (%rsi), %dl
    cmpb    $'0', %dl
    jb      3f
    cmpb    $'9', %dl
    ja      3f
    subb    $'0', %dl
    imulq   $10, %rax
    movzbq  %dl, %rdx
    addq    %rdx, %rax
    incq    %rsi
    jmp     2b
3:  ret
gs_mem_skip:
gs_mem_end:
    popq    %r12
    ret

# ═══════════════════════════════════════════════════════
#  MAIN
# ═══════════════════════════════════════════════════════

_start:
    call    gather_sys

    # ── print header ──
    movq    $1, %rdi                # fd = stdout
    call    print_title

    # ── host@os line ──
    movq    $1, %rdi
    call    print_hostline

    # ── info lines ──
    movq    $1, %rdi
    leaq    lbl_os(%rip), %rsi
    leaq    out_os(%rip), %rdx
    call    print_labeled

    movq    $1, %rdi
    leaq    lbl_wm(%rip), %rsi
    leaq    out_wm(%rip), %rdx
    call    print_labeled

    movq    $1, %rdi
    leaq    lbl_kernel(%rip), %rsi
    leaq    out_kernel(%rip), %rdx
    call    print_labeled

    movq    $1, %rdi
    leaq    lbl_uptime(%rip), %rsi
    leaq    out_uptime(%rip), %rdx
    call    print_labeled

    movq    $1, %rdi
    leaq    lbl_cpu(%rip), %rsi
    leaq    out_cpu(%rip), %rdx
    call    print_labeled

    movq    $1, %rdi
    leaq    lbl_mem(%rip), %rsi
    leaq    out_mem(%rip), %rdx
    call    print_labeled

    # ── final newline ──
    leaq    str_newline(%rip), %rsi
    movq    $1, %rdi
    call    printz

    # ── exit ──
    movq    $60, %rax
    xorq    %rdi, %rdi
    syscall
