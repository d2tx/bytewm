# bytelock - standalone x86_64 screen locker for bytewm
# gruvbox themed, PAM auth, X11 drawing
# build: as -o bytelock.o bytelock.s && cc -o bytelock bytelock.o -lX11 -lpam

.section .rodata
pam_service:     .asciz  "su"
font_name:       .asciz  "fixed"
str_user:        .asciz  "USER"
str_root:        .asciz  "root"

color_bg:        .asciz  "#282828"
color_fg:        .asciz  "#ebdbb2"
color_aqua:      .asciz  "#689d6a"
color_red:       .asciz  "#cc241d"
color_orange:    .asciz  "#d65d0e"
color_dimbg:     .asciz  "#3c3836"

str_locked:      .asciz  "-- LOCKED --"
str_incorrect:   .asciz  "incorrect"
str_dots:        .ascii  "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * "
                .byte 0
.equ DOT_STRIDE, 2

# debug
logpath:         .asciz  "/tmp/bytelock.log"
log_conv_called: .asciz  "[conv] called\n"
log_auth_ret:    .asciz  "[auth] pam_authenticate returned: "
log_start_fail:  .asciz  "[auth] pam_start FAILED\n"
log_pw:          .asciz  "[conv] password=["
log_end:         .asciz  "]\n"
log_style:       .asciz  "[conv] msg_style="
log_sp:          .asciz  " "
log_nl:          .asciz  "\n"

.section .data
# env pointer (set at startup)
environ_ptr:     .quad   0

# PAM
pam_handle:      .quad   0
conv_func:       .quad   pam_converse
conv_appdata:    .quad   0

# X11 handles
xdpy:            .quad   0
xscr:            .quad   0
xroot:           .quad   0
xwin:            .quad   0
xgc:             .quad   0
xfont:           .quad   0
xcmap:           .quad   0
xsw:             .quad   0
xsh:             .quad   0
xdepth:          .quad   0
xvisual:         .quad   0

# colors (allocated pixels)
p_bg:            .quad   0
p_fg:            .quad   0
p_aqua:          .quad   0
p_red:           .quad   0
p_orange:        .quad   0
p_dimbg:         .quad   0

# state
pwd_len:         .quad   0
auth_error:      .quad   0

.equ PAM_SUCCESS,       0
.equ PAM_PROMPT_ECHO_OFF, 1
.equ PAM_PROMPT_ECHO_ON,  2
.equ PAM_ERROR_MSG,     3
.equ PAM_TEXT_INFO,      4
.equ PAM_AUTH_ERR,       7

.equ GrabModeAsync,     1
.equ CWOverrideRedirect, 512
.equ ExposureMask,      32768
.equ KeyPressMask,      1
.equ ButtonPressMask,   4
.equ PointerMotionMask, 64
.equ CurrentTime,       0
.equ KeyPress,          2
.equ Expose,            12

.section .bss
password:        .fill   256, 1, 0
username:        .fill   64,  1, 0
resp_buf:        .fill   64,  1, 0
keybuf:          .fill   32,  1, 0
xev:             .fill   192, 1, 0

.section .text
.globl main
.extern XOpenDisplay, XDefaultScreen, XRootWindow, XDefaultColormap
.extern XDefaultDepth, XDefaultVisual, XLoadQueryFont
.extern XParseColor, XAllocColor, XCreateWindow
.extern XCreateGC, XSetForeground, XSetFont, XFillRectangle
.extern XDrawString, XDrawRectangle, XTextWidth, XMapWindow, XRaiseWindow
.extern XSelectInput, XNextEvent, XGrabKeyboard, XGrabPointer
.extern XUngrabKeyboard, XUngrabPointer, XBell, XFlush, XSync
.extern XDestroyWindow, XFreeGC, XFreeFont, XCloseDisplay
.extern pam_start, pam_authenticate, pam_end
.extern pam_acct_mgmt
.extern calloc, strdup, free, usleep

# ═══════════════════════════════════════════════════════
#  HELPERS
# ═══════════════════════════════════════════════════════

_strlen:
    xorq    %rcx, %rcx
    decq    %rcx
1:  incq    %rcx
    cmpb    $0, (%rdi, %rcx)
    jne     1b
    movq    %rcx, %rax
    ret

# getenv_r(%rdi = name) → %rax = value (NULL if not found)
getenv_r:
    pushq   %r12
    pushq   %r13
    pushq   %rbx
    movq    %rdi, %rbx
    call    _strlen
    movq    %rax, %r12
    movq    environ_ptr(%rip), %r13
    testq   %r13, %r13
    jz      ge_nf
1:  movq    (%r13), %rdi
    testq   %rdi, %rdi
    jz      ge_nf
    # strncmp(name, env[i], name_len)
    movq    %rbx, %rsi
    movq    %rdi, %rcx
    movq    %r12, %rdx
    # simple byte-by-byte comparison
    pushq   %rdi
ge_cmp:
    testq   %rdx, %rdx
    jz      ge_check_eq
    movb    (%rsi), %al
    cmpb    (%rcx), %al
    jne     ge_next
    incq    %rsi
    incq    %rcx
    decq    %rdx
    jmp     ge_cmp
ge_check_eq:
    cmpb    $'=', (%rcx)
    je      ge_found
ge_next:
    popq    %rdi
    addq    $8, %r13
    jmp     1b
ge_found:
    popq    %rdi
    leaq    1(%rcx), %rax
    jmp     ge_done
ge_nf:
    xorq    %rax, %rax
ge_done:
    popq    %rbx
    popq    %r13
    popq    %r12
    ret

# ═══════════════════════════════════════════════════════
#  PAM CONVERSATION CALLBACK
#  rdi=num_msg  rsi=msg  rdx=resp  rcx=appdata
# ═══════════════════════════════════════════════════════

pam_converse:
    pushq   %rbp
    movq    %rsp, %rbp
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
    pushq   %rbx

    movq    %rdi, %r12             # num_msg
    movq    %rsi, %r13             # msg
    movq    %rdx, %r14             # resp out

    # calloc(num_msg, 16) — 16 bytes per pam_response
    movq    %r12, %rdi
    movq    $16, %rsi
    call    calloc
    movq    %rax, %rbx             # response array

    xorq    %r15, %r15             # i = 0

pc_loop:
    cmpq    %r12, %r15
    jae     pc_end

    movq    (%r13, %r15, 8), %rdi  # msg[i]
    movl    (%rdi), %eax            # msg_style

    cmpl    $PAM_PROMPT_ECHO_OFF, %eax
    je      pc_password
    cmpl    $PAM_PROMPT_ECHO_ON, %eax
    je      pc_username

    # info/error: empty response — compute address fresh
    movq    %r15, %rax
    shlq    $4, %rax
    movq    $0, (%rbx, %rax)
    movl    $0, 8(%rbx, %rax)
    jmp     pc_next

pc_password:
    leaq    password(%rip), %rdi
    call    strdup                  # clobbers rdx/rcx
    movq    %r15, %rdx
    shlq    $4, %rdx
    movq    %rax, (%rbx, %rdx)     # resp[i].resp
    movl    $0, 8(%rbx, %rdx)       # resp[i].resp_retcode
    jmp     pc_next

pc_username:
    leaq    username(%rip), %rdi
    call    strdup
    movq    %r15, %rdx
    shlq    $4, %rdx
    movq    %rax, (%rbx, %rdx)
    movl    $0, 8(%rbx, %rdx)
    jmp     pc_next

pc_next:
    incq    %r15
    jmp     pc_loop

pc_end:
    movq    %rbx, (%r14)           # *resp = response array
    xorq    %rax, %rax             # PAM_SUCCESS

    popq    %rbx
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbp
    ret

# ═══════════════════════════════════════════════════════
#  AUTHENTICATE  → rax = 1 (ok) / 0 (fail)
# ═══════════════════════════════════════════════════════

try_auth:
    pushq   %r12
    pushq   %r13
    subq    $8, %rsp               # align stack for pam_start call

    leaq    pam_service(%rip), %rdi
    leaq    username(%rip), %rsi
    cmpb    $0, (%rsi)
    jne     1f
    leaq    str_root(%rip), %rsi
1:
    leaq    conv_func(%rip), %rdx
    leaq    pam_handle(%rip), %rcx
    call    pam_start
    testl   %eax, %eax
    jnz     ta_fail

    movq    pam_handle(%rip), %rdi
    xorq    %rsi, %rsi
    call    pam_authenticate
    movl    %eax, %r12d

    testl   %eax, %eax
    jnz     ta_end

    # check account validity (expired/locked/disabled)
    movq    pam_handle(%rip), %rdi
    xorq    %rsi, %rsi
    call    pam_acct_mgmt
    movl    %eax, %r12d

ta_end:
    movq    pam_handle(%rip), %rdi
    movl    %r12d, %esi
    call    pam_end

    movq    $0, pam_handle(%rip)

    testl   %r12d, %r12d
    jnz     ta_fail

    movq    $1, %rax
    jmp     ta_done
ta_fail:
    xorq    %rax, %rax
ta_done:
    addq    $8, %rsp               # undo alignment
    popq    %r13
    popq    %r12
    ret

# ═══════════════════════════════════════════════════════
#  DRAW LOCK SCREEN
# ═══════════════════════════════════════════════════════

draw_lock:
    pushq   %rbp
    movq    %rsp, %rbp
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
    pushq   %rbx
    subq    $8, %rsp               # align stack (6 pushes = 8 mod 16)

    # fill background
    movq    xdpy(%rip), %rdi
    movq    xgc(%rip), %rsi
    movq    p_bg(%rip), %rdx
    call    XSetForeground
    subq    $8, %rsp
    pushq   xsh(%rip)
    movq    xdpy(%rip), %rdi
    movq    xwin(%rip), %rsi
    movq    xgc(%rip), %rdx
    xorq    %rcx, %rcx
    xorq    %r8,  %r8
    movq    xsw(%rip), %r9
    call    XFillRectangle
    addq    $16, %rsp

    # font height
    movq    xfont(%rip), %rax
    movl    88(%rax), %r14d        # r14d = ascent
    movl    92(%rax), %r15d        # r15d = descent
    addl    %r15d, %r14d           # r14d = ascent + descent
    movslq  %r14d, %r14            # r14 = bh

    # ── measure title ──
    leaq    str_locked(%rip), %rdi
    call    _strlen
    movq    %rax, %r15             # r15 = title_len
    movq    xfont(%rip), %rdi
    leaq    str_locked(%rip), %rsi
    movq    %r15, %rdx
    call    XTextWidth
    movq    %rax, %r12             # r12 = title_w

    # ── measure dots ──
    movq    pwd_len(%rip), %r13
    testq   %r13, %r13
    jz      dl_nodots_meas
    imulq   $DOT_STRIDE, %r13
    movq    xfont(%rip), %rdi
    leaq    str_dots(%rip), %rsi
    movq    %r13, %rdx
    call    XTextWidth
    jmp     dl_dots_meas_done
dl_nodots_meas:
    xorq    %rax, %rax
dl_dots_meas_done:
    movq    %rax, %r13             # r13 = dots_w (0 if no dots)

    # ── measure error ──
    cmpq    $0, auth_error(%rip)
    je      dl_noerr_meas
    leaq    str_incorrect(%rip), %rdi
    call    _strlen
    movq    xfont(%rip), %rdi
    leaq    str_incorrect(%rip), %rsi
    movq    %rax, %rdx
    call    XTextWidth
    jmp     dl_err_meas_done
dl_noerr_meas:
    xorq    %rax, %rax
dl_err_meas_done:
    movq    %rax, %r15             # r15 = error_w (0 if no error)

    # ── card dimensions ──
    # card_w = max(title_w, dots_w, error_w) + 80
    movq    %r12, %rax             # title_w
    cmpq    %rax, %r13
    cmova   %r13, %rax
    cmpq    %rax, %r15
    cmova   %r15, %rax
    addq    $80, %rax
    movq    %rax, %rbx             # rbx = card_w

    # card content height
    movq    %r14, %rax             # title line (bh)
    addq    $12, %rax              # gap
    addq    %r14, %rax             # dots line (bh)
    addq    $12, %rax              # gap
    addq    %r14, %rax             # error line (bh)
    addq    $24, %rax              # top+bottom padding inside card
    movq    %rax, %r15             # r15 = card_h

    # card position
    movq    xsw(%rip), %rax
    subq    %rbx, %rax
    shrq    $1, %rax               # card_x
    movq    %rax, %r12             # save card_x

    movq    xsh(%rip), %rax
    subq    %r15, %rax
    shrq    $1, %rax
    movq    %rax, %r13             # r13 = card_y

    # ── draw card border (dimbg) ──
    movq    xdpy(%rip), %rdi
    movq    xgc(%rip), %rsi
    movq    p_dimbg(%rip), %rdx
    call    XSetForeground

    pushq   %r15                   # h
    movq    xdpy(%rip), %rdi
    movq    xwin(%rip), %rsi
    movq    xgc(%rip), %rdx
    movq    %r12, %rcx
    movq    %r13, %r8
    movq    %rbx, %r9              # w
    call    XDrawRectangle
    addq    $8, %rsp

    # ── draw title (orange, centered in card) ──
    leaq    str_locked(%rip), %rdi
    call    _strlen
    movq    %rax, %r15

    movq    xfont(%rip), %rdi
    leaq    str_locked(%rip), %rsi
    movq    %r15, %rdx
    call    XTextWidth

    movq    xsw(%rip), %rcx
    subq    %rax, %rcx
    shrq    $1, %rcx               # title_x

    movq    xfont(%rip), %rax
    movslq  88(%rax), %rax
    addq    %r13, %rax
    addq    $12, %rax
    movq    %rax, %r8              # title_y

    subq    $8, %rsp
    pushq   %rcx
    pushq   %r8
    pushq   %r15

    movq    xdpy(%rip), %rdi
    movq    xgc(%rip), %rsi
    movq    p_orange(%rip), %rdx
    call    XSetForeground

    popq    %r9
    popq    %r8
    popq    %rcx
    addq    $8, %rsp

    pushq   $0
    pushq   %r9
    movq    xdpy(%rip), %rdi
    movq    xwin(%rip), %rsi
    movq    xgc(%rip), %rdx
    leaq    str_locked(%rip), %r9
    call    XDrawString
    addq    $16, %rsp

    # ── draw dots (fg, centered in card) ──
    movq    pwd_len(%rip), %r15
    testq   %r15, %r15
    jz      dl_skip_dots

    imulq   $DOT_STRIDE, %r15
    movq    xfont(%rip), %rdi
    leaq    str_dots(%rip), %rsi
    movq    %r15, %rdx
    call    XTextWidth

    movq    xsw(%rip), %rcx
    subq    %rax, %rcx
    shrq    $1, %rcx               # dots_x

    movq    xfont(%rip), %rax
    movslq  88(%rax), %rax
    addq    %r13, %rax
    addq    %r14, %rax
    addq    $24, %rax
    movq    %rax, %r8              # dots_y

    subq    $8, %rsp
    pushq   %rcx
    pushq   %r8
    pushq   %r15

    movq    xdpy(%rip), %rdi
    movq    xgc(%rip), %rsi
    movq    p_fg(%rip), %rdx
    call    XSetForeground

    popq    %r9
    popq    %r8
    popq    %rcx
    addq    $8, %rsp

    pushq   $0
    pushq   %r9
    movq    xdpy(%rip), %rdi
    movq    xwin(%rip), %rsi
    movq    xgc(%rip), %rdx
    leaq    str_dots(%rip), %r9
    call    XDrawString
    addq    $16, %rsp

dl_skip_dots:

    # ── draw error (red, centered in card) ──
    cmpq    $0, auth_error(%rip)
    je      dl_flush

    leaq    str_incorrect(%rip), %rdi
    call    _strlen
    movq    %rax, %r15
    movq    xfont(%rip), %rdi
    leaq    str_incorrect(%rip), %rsi
    movq    %r15, %rdx
    call    XTextWidth

    movq    xsw(%rip), %rcx
    subq    %rax, %rcx
    shrq    $1, %rcx               # error_x

    movq    xfont(%rip), %rax
    movslq  88(%rax), %rax
    addq    %r13, %rax
    addq    %r14, %rax
    addq    %r14, %rax
    addq    $36, %rax
    movq    %rax, %r8              # error_y

    subq    $8, %rsp
    pushq   %rcx
    pushq   %r8
    pushq   %r15

    movq    xdpy(%rip), %rdi
    movq    xgc(%rip), %rsi
    movq    p_red(%rip), %rdx
    call    XSetForeground

    popq    %r9
    popq    %r8
    popq    %rcx
    addq    $8, %rsp

    pushq   $0
    pushq   %r9
    movq    xdpy(%rip), %rdi
    movq    xwin(%rip), %rsi
    movq    xgc(%rip), %rdx
    leaq    str_incorrect(%rip), %r9
    call    XDrawString
    addq    $16, %rsp

dl_flush:
    movq    xdpy(%rip), %rdi
    call    XFlush
    addq    $8, %rsp               # undo alignment
    popq    %rbx
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbp
    ret

# ═══════════════════════════════════════════════════════
#  MAIN
# ═══════════════════════════════════════════════════════

main:
    pushq   %rbp
    movq    %rsp, %rbp
    pushq   %r12
    pushq   %r13
    pushq   %r14
    subq    $8, %rsp               # align stack to 16 bytes for C calls

    movq    %rdx, environ_ptr(%rip) # save envp

    # disable core dumps to prevent password leakage
    movq    $157, %rax              # sys_prctl
    movq    $2, %rdi                # PR_SET_DUMPABLE
    xorq    %rsi, %rsi              # 0 = disable
    xorq    %rdx, %rdx
    xorq    %r10, %r10
    xorq    %r8, %r8
    syscall

    # mlock password buffer to prevent swap leakage
    movq    $149, %rax              # sys_mlock
    leaq    password(%rip), %rdi
    andq    $-4096, %rdi            # page-align down
    movq    $4096, %rsi             # one page is enough
    syscall

    # ── get username ──
    leaq    str_user(%rip), %rdi
    call    getenv_r
    testq   %rax, %rax
    jz      1f
    # copy username
    movq    %rax, %rsi
    leaq    username(%rip), %rdi
    xorq    %rcx, %rcx              # byte counter
2:  movb    (%rsi), %al
    movb    %al, (%rdi)
    testb   %al, %al
    jz      1f
    incq    %rsi
    incq    %rdi
    incq    %rcx
    cmpq    $63, %rcx               # hard cap at 63 bytes
    jae     1f
    jmp     2b
1:

    # ── open display ──
    xorq    %rdi, %rdi
    call    XOpenDisplay
    movq    %rax, xdpy(%rip)
    testq   %rax, %rax
    jz      exit_fail

    # screen / root
    movq    %rax, %rdi
    call    XDefaultScreen
    movl    %eax, xscr(%rip)
    movslq  %eax, %rsi

    movq    xdpy(%rip), %rdi
    movslq  xscr(%rip), %rsi
    call    XRootWindow
    movq    %rax, xroot(%rip)

    movq    xdpy(%rip), %rdi
    movslq  xscr(%rip), %rsi
    call    XDefaultColormap
    movq    %rax, xcmap(%rip)

    movq    xdpy(%rip), %rdi
    movslq  xscr(%rip), %rsi
    call    XDefaultDepth
    movl    %eax, xdepth(%rip)

    movq    xdpy(%rip), %rdi
    movslq  xscr(%rip), %rsi
    call    XDefaultVisual
    movq    %rax, xvisual(%rip)

    # screen dimensions
    movq    xdpy(%rip), %rdi
    movslq  xscr(%rip), %rsi
    call    XRootWindow             # already have it, but we need sw/sh
    # Actually, get screen dimensions via DisplayWidth/Height
    # These are macros, call the functions:
    .extern XDisplayWidth, XDisplayHeight
    movq    xdpy(%rip), %rdi
    movslq  xscr(%rip), %rsi
    call    XDisplayWidth
    movq    %rax, xsw(%rip)
    movq    xdpy(%rip), %rdi
    movslq  xscr(%rip), %rsi
    call    XDisplayHeight
    movq    %rax, xsh(%rip)

    # ── allocate colors ──
    # bg
    leaq    color_bg(%rip), %rsi
    leaq    xev(%rip), %rdx         # use xev buffer as temp XColor
    movq    xdpy(%rip), %rdi
    movq    xcmap(%rip), %rsi
    movq    %rsi, %r8
    leaq    color_bg(%rip), %rdx
    leaq    xev(%rip), %rcx
    call    XParseColor
    movq    xdpy(%rip), %rdi
    movq    xcmap(%rip), %rsi
    leaq    xev(%rip), %rdx
    call    XAllocColor
    movq    xev(%rip), %rax         # pixel value
    movq    %rax, p_bg(%rip)
    # fg
    movq    xdpy(%rip), %rdi
    movq    xcmap(%rip), %rsi
    leaq    color_fg(%rip), %rdx
    leaq    xev(%rip), %rcx
    call    XParseColor
    movq    xdpy(%rip), %rdi
    movq    xcmap(%rip), %rsi
    leaq    xev(%rip), %rdx
    call    XAllocColor
    movq    xev(%rip), %rax
    movq    %rax, p_fg(%rip)
    # aqua
    movq    xdpy(%rip), %rdi
    movq    xcmap(%rip), %rsi
    leaq    color_aqua(%rip), %rdx
    leaq    xev(%rip), %rcx
    call    XParseColor
    movq    xdpy(%rip), %rdi
    movq    xcmap(%rip), %rsi
    leaq    xev(%rip), %rdx
    call    XAllocColor
    movq    xev(%rip), %rax
    movq    %rax, p_aqua(%rip)
    # red
    movq    xdpy(%rip), %rdi
    movq    xcmap(%rip), %rsi
    leaq    color_red(%rip), %rdx
    leaq    xev(%rip), %rcx
    call    XParseColor
    movq    xdpy(%rip), %rdi
    movq    xcmap(%rip), %rsi
    leaq    xev(%rip), %rdx
    call    XAllocColor
    movq    xev(%rip), %rax
    movq    %rax, p_red(%rip)
    # orange
    movq    xdpy(%rip), %rdi
    movq    xcmap(%rip), %rsi
    leaq    color_orange(%rip), %rdx
    leaq    xev(%rip), %rcx
    call    XParseColor
    movq    xdpy(%rip), %rdi
    movq    xcmap(%rip), %rsi
    leaq    xev(%rip), %rdx
    call    XAllocColor
    movq    xev(%rip), %rax
    movq    %rax, p_orange(%rip)
    # dimbg
    movq    xdpy(%rip), %rdi
    movq    xcmap(%rip), %rsi
    leaq    color_dimbg(%rip), %rdx
    leaq    xev(%rip), %rcx
    call    XParseColor
    movq    xdpy(%rip), %rdi
    movq    xcmap(%rip), %rsi
    leaq    xev(%rip), %rdx
    call    XAllocColor
    movq    xev(%rip), %rax
    movq    %rax, p_dimbg(%rip)

    # ── load font ──
    movq    xdpy(%rip), %rdi
    leaq    font_name(%rip), %rsi
    call    XLoadQueryFont
    movq    %rax, xfont(%rip)
    testq   %rax, %rax
    jz      exit_fail

    # ── create fullscreen window ──
    # XCreateWindow(dpy, parent, x, y, w, h, border, depth, class, visual, mask, attrs)
    # 12 args → 6 on stack (pushed in reverse: attrs, mask, visual, class, depth, border)
    movq    xdpy(%rip), %rdi
    movq    xroot(%rip), %rsi
    xorq    %rdx, %rdx
    xorq    %rcx, %rcx
    movq    xsw(%rip), %r8
    movq    xsh(%rip), %r9
    # stack attrs: build a minimal XSetWindowAttributes with override_redirect=1
    subq    $112, %rsp             # XSetWindowAttributes = 112 bytes
    movq    %rsp, %r12
    # zero the struct
    movq    $0, (%r12)
    movq    $0, 8(%r12)
    movq    $0, 16(%r12)
    movq    $0, 24(%r12)
    movq    $0, 32(%r12)
    movq    $0, 40(%r12)
    movq    $0, 48(%r12)
    movq    $0, 56(%r12)
    movq    $0, 64(%r12)
    movq    $0, 72(%r12)
    movq    $0, 80(%r12)
    movq    $1, 88(%r12)           # override_redirect = True
    movq    $0, 96(%r12)
    movq    $0, 104(%r12)
    # push stack args in reverse order
    pushq   %r12                   # attrs
    pushq   $CWOverrideRedirect    # valuemask
    pushq   xvisual(%rip)          # visual
    pushq   $1                     # class = InputOutput
    pushq   xdepth(%rip)           # depth
    pushq   $0                     # border_width
    call    XCreateWindow
    addq    $48, %rsp              # clean 6 pushes
    addq    $112, %rsp             # clean attrs struct
    movq    %rax, xwin(%rip)

    # ── create GC ──
    movq    xdpy(%rip), %rdi
    movq    xwin(%rip), %rsi
    xorq    %rdx, %rdx             # valuemask = 0
    xorq    %rcx, %rcx             # values = NULL
    call    XCreateGC
    movq    %rax, xgc(%rip)

    # set font on GC (XSetFont expects Font ID, not XFontStruct*)
    movq    xfont(%rip), %rax
    movq    8(%rax), %rdx          # font->fid
    movq    xdpy(%rip), %rdi
    movq    xgc(%rip), %rsi
    call    XSetFont

    # ── show window ──
    movq    xdpy(%rip), %rdi
    movq    xwin(%rip), %rsi
    call    XMapWindow
    movq    xdpy(%rip), %rdi
    movq    xwin(%rip), %rsi
    call    XRaiseWindow

    # set event mask
    movq    xdpy(%rip), %rdi
    movq    xwin(%rip), %rsi
    movq    $(ExposureMask | KeyPressMask), %rdx
    call    XSelectInput

    # ── grab keyboard (retry up to 5x with 50ms delay) ──
    xorq    %r12, %r12             # r12 = retry counter
grab_retry:
    cmpq    $5, %r12
    jb      1f
    movq    $1, %rax               # exit code for grab failure
    jmp     cleanup
1:

    movq    xdpy(%rip), %rdi
    movq    xroot(%rip), %rsi
    movq    $1, %rdx
    movq    $GrabModeAsync, %rcx
    movq    $GrabModeAsync, %r8
    movq    $CurrentTime, %r9
    call    XGrabKeyboard
    testl   %eax, %eax
    jz      grab_kb_ok

    incq    %r12
    movq    $50000, %rdi            # 50ms
    call    usleep
    jmp     grab_retry
grab_kb_ok:

    # XGrabPointer (9 args → 3 on stack, pad for alignment)
    subq    $8, %rsp               # alignment pad (need even pushes before call)
    pushq   $CurrentTime
    pushq   $0                     # cursor = None
    pushq   xroot(%rip)            # confine_to
    movq    xdpy(%rip), %rdi
    movq    xroot(%rip), %rsi
    movq    $1, %rdx
    movq    $(ButtonPressMask | PointerMotionMask), %rcx
    movq    $GrabModeAsync, %r8
    movq    $GrabModeAsync, %r9
    call    XGrabPointer
    addq    $32, %rsp              # clean 4 slots
    testl   %eax, %eax
    jnz     cleanup

    # draw initial screen
    call    draw_lock

    # ── event loop ──
event_loop:
    movq    xdpy(%rip), %rdi
    leaq    xev(%rip), %rsi
    call    XNextEvent

    movl    xev(%rip), %eax        # event type
    cmpl    $Expose, %eax
    je      ev_expose
    cmpl    $KeyPress, %eax
    je      ev_key
    jmp     event_loop

ev_expose:
    cmpl    $0, xev+56(%rip)       # xexpose.count
    jne     event_loop
    call    draw_lock
    jmp     event_loop

ev_key:
    # XLookupString(event, buf, bufsize, &keysym, NULL)
    leaq    xev(%rip), %rdi
    leaq    keybuf(%rip), %rsi
    movq    $32, %rdx
    leaq    xev+32(%rip), %rcx     # keysym ptr (use spare space in xev)
    xorq    %r8, %r8               # compose = NULL
    .extern XLookupString
    call    XLookupString

    movq    xev+32(%rip), %r12     # keysym

    # ── Enter: authenticate ──
    cmpq    $0xFF0D, %r12          # XK_Return
    je      ev_enter

    # ── Escape: clear password ──
    cmpq    $0xFF1B, %r12          # XK_Escape
    je      ev_clear

    # ── BackSpace: delete last char ──
    cmpq    $0xFF08, %r12          # XK_BackSpace
    je      ev_backspace

    # ── Ctrl+U: clear ──
    cmpq    $0x15, %r12            # Ctrl+U
    je      ev_clear

    # ── printable ──
    cmpq    $0x20, %r12
    jb      event_loop
    cmpq    $0x7E, %r12
    ja      event_loop

    # append char
    movq    pwd_len(%rip), %rcx
    cmpq    $32, %rcx               # cap to match dots string
    jae     event_loop
    movb    keybuf(%rip), %al
    leaq    password(%rip), %rdi
    movb    %al, (%rdi, %rcx)
    incq    %rcx
    movq    %rcx, pwd_len(%rip)
    movb    $0, (%rdi, %rcx)
    movq    $0, auth_error(%rip)
    call    draw_lock
    jmp     event_loop

ev_backspace:
    movq    pwd_len(%rip), %rcx
    testq   %rcx, %rcx
    jz      event_loop
    decq    %rcx
    movq    %rcx, pwd_len(%rip)
    leaq    password(%rip), %rdi
    movb    $0, (%rdi, %rcx)
    movq    $0, auth_error(%rip)
    call    draw_lock
    jmp     event_loop

ev_clear:
    movq    $0, pwd_len(%rip)
    movb    $0, password(%rip)
    movq    $0, auth_error(%rip)
    call    draw_lock
    jmp     event_loop

ev_enter:
    # check if password is empty
    cmpq    $0, pwd_len(%rip)
    je      event_loop

    # try authentication
    call    try_auth
    cmpq    $1, %rax
    je      cleanup

    # auth failed: error, clear password
    movq    $1, auth_error(%rip)
    movq    $0, pwd_len(%rip)
    movb    $0, password(%rip)
    movq    xdpy(%rip), %rdi
    movq    $50, %rsi
    call    XBell
    call    draw_lock
    jmp     event_loop

# ── cleanup & exit ──
cleanup:
    movq    xdpy(%rip), %rdi
    movq    $CurrentTime, %rsi
    call    XUngrabKeyboard
    movq    xdpy(%rip), %rdi
    movq    $CurrentTime, %rsi
    call    XUngrabPointer

    movq    xdpy(%rip), %rdi
    movq    xgc(%rip), %rsi
    call    XFreeGC
    movq    xdpy(%rip), %rdi
    movq    xfont(%rip), %rsi
    call    XFreeFont
    movq    xdpy(%rip), %rdi
    movq    xwin(%rip), %rsi
    call    XDestroyWindow
    movq    xdpy(%rip), %rdi
    call    XCloseDisplay

    # zero password buffer (all 256 bytes)
    leaq    password(%rip), %rdi
    xorq    %rcx, %rcx
1:  movb    $0, (%rdi, %rcx)
    incq    %rcx
    cmpq    $256, %rcx
    jne     1b

    xorq    %rax, %rax              # exit 0 = success
    jmp     exit_ret

exit_fail:
    movq    $1, %rax
exit_ret:
    addq    $8, %rsp               # undo stack alignment
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbp
    # sys_exit directly to avoid libc cleanup issues
    movq    $60, %rax
    xorq    %rdi, %rdi
    syscall
