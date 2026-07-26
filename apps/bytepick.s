# bytepick - x86_64 color picker for bytewm
# gruvbox themed, X11-linked, copies hex to clipboard via xclip
# build: as -o bytepick.o bytepick.s && cc -o bytepick bytepick.o -lX11

.section .rodata

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
path_xclip:      .asciz  "/usr/bin/xclip"
path_fifo:       .asciz  "/tmp/bytify.fifo"

str_prompt:      .asciz  "\033[38;5;246m  click any pixel (Esc to cancel)  \033[0m\n"
str_cancelled:   .asciz  "\033[38;5;246m  cancelled  \033[0m\n"
str_no_display:  .asciz  "bytepick: could not open display\n"
str_sp:          .asciz  "  "

clr_aqua:        .ascii  "\033[38;5;108m"
clr_reset_nl:    .asciz  "\033[0m\n"

.equ CLR_AQUA_LEN,  11

.equ ButtonPressMask,   4
.equ KeyPressMask,      1
.equ ButtonPress,       4
.equ KeyPress,          2
.equ GrabModeAsync,     1
.equ CurrentTime,       0
.equ ZPixmap,           2

.section .data
envp_save:       .quad   0
xdpy:            .quad   0
xroot:           .quad   0
ximg:            .quad   0
red_val:         .quad   0
green_val:       .quad   0
blue_val:        .quad   0
pixel_val:       .quad   0

.section .bss
xev_buf:         .fill   192, 1, 0
outbuf:          .fill   128, 1, 0

.section .text
.globl main

.extern XOpenDisplay, XDefaultScreen, XRootWindow
.extern XGrabPointer, XGrabKeyboard
.extern XUngrabPointer, XUngrabKeyboard
.extern XNextEvent, XCloseDisplay, XSync
.extern XGetImage, XGetPixel, XDestroyImage

_strlen:
    xorq    %rcx, %rcx
    decq    %rcx
1:  incq    %rcx
    cmpb    $0, (%rdi, %rcx)
    jne     1b
    movq    %rcx, %rax
    ret

print_raw:
    movq    $1, %rax
    syscall
    ret

printz:
    pushq   %rdi
    call    _strlen
    movq    %rax, %rdx
    popq    %rsi
    movq    $1, %rax
    movq    $1, %rdi
    syscall
    ret

hex_nibble:
    cmpb    $10, %al
    jb      1f
    addb    $'a' - 10, %al
    ret
1:  addb    $'0', %al
    ret

hex_byte:
    pushq   %r12
    pushq   %rdi
    movq    %rax, %r12
    shrb    $4, %al
    call    hex_nibble
    popq    %rdi
    movb    %al, (%rdi)
    movq    %r12, %rax
    andb    $0x0F, %al
    call    hex_nibble
    movb    %al, 1(%rdi)
    popq    %r12
    ret

tzcnt:
    bsfq    %rax, %rax
    jnz     1f
    xorq    %rax, %rax
1:  ret

extract_rgb:
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %rbx

    movq    ximg(%rip), %r12
    movq    56(%r12), %r13
    movq    64(%r12), %r14
    movq    pixel_val(%rip), %r12

    # red
    movq    %r13, %rax
    call    tzcnt
    movq    %rax, %rcx
    movq    %r13, %rdi
    shrq    %cl, %rdi
    movq    %r12, %rax
    andq    %r13, %rax
    shrq    %cl, %rax
    testq   %rdi, %rdi
    jz      1f
    pushq   %rdx
    imulq   $255, %rax
    xorq    %rdx, %rdx
    divq    %rdi
    popq    %rdx
1:  movq    %rax, red_val(%rip)

    # green
    movq    %r14, %rax
    call    tzcnt
    movq    %rax, %rcx
    movq    %r14, %rdi
    shrq    %cl, %rdi
    movq    %r12, %rax
    andq    %r14, %rax
    shrq    %cl, %rax
    testq   %rdi, %rdi
    jz      1f
    pushq   %rdx
    imulq   $255, %rax
    xorq    %rdx, %rdx
    divq    %rdi
    popq    %rdx
1:  movq    %rax, green_val(%rip)

    # blue
    movq    ximg(%rip), %rdi
    movq    72(%rdi), %rbx
    movq    %rbx, %rax
    call    tzcnt
    movq    %rax, %rcx
    movq    %rbx, %rdi
    shrq    %cl, %rdi
    movq    %r12, %rax
    andq    %rbx, %rax
    shrq    %cl, %rax
    testq   %rdi, %rdi
    jz      1f
    pushq   %rdx
    imulq   $255, %rax
    xorq    %rdx, %rdx
    divq    %rdi
    popq    %rdx
1:  movq    %rax, blue_val(%rip)

    popq    %rbx
    popq    %r14
    popq    %r13
    popq    %r12
    ret

format_hex:
    leaq    outbuf(%rip), %rdi
    movb    $'#', (%rdi)

    movq    red_val(%rip), %rax
    leaq    1(%rdi), %rdi
    call    hex_byte

    movq    green_val(%rip), %rax
    leaq    2(%rdi), %rdi
    call    hex_byte

    movq    blue_val(%rip), %rax
    leaq    2(%rdi), %rdi
    call    hex_byte

    leaq    2(%rdi), %rdi
    movb    $'\n', (%rdi)
    movb    $0,    1(%rdi)
    ret

clip_copy:
    pushq   %r12
    pushq   %r13

    subq    $16, %rsp
    movq    %rsp, %rdi
    movq    $22, %rax
    syscall
    testq   %rax, %rax
    js      cc_fail

    movl    0(%rsp), %r12d
    movl    4(%rsp), %r13d

    movq    $57, %rax
    syscall
    testq   %rax, %rax
    js      cc_close_both
    jz      cc_child

    movq    $3, %rax
    movl    %r12d, %edi
    syscall

    leaq    outbuf(%rip), %rdi
    call    _strlen
    movq    %rax, %rdx
    movq    $1, %rax
    leaq    outbuf(%rip), %rsi
    movl    %r13d, %edi
    syscall

    movq    $3, %rax
    movl    %r13d, %edi
    syscall
    addq    $16, %rsp
    popq    %r13
    popq    %r12
    ret

cc_child:
    movq    $3, %rax
    movl    %r13d, %edi
    syscall

    movq    $33, %rax
    movl    %r12d, %edi
    xorq    %rsi, %rsi
    syscall

    movq    $3, %rax
    movl    %r12d, %edi
    syscall

    movq    $59, %rax
    leaq    path_xclip(%rip), %rdi
    leaq    xclip_argv(%rip), %rsi
    movq    envp_save(%rip), %rdx
    syscall

    movq    $60, %rax
    movq    $1, %rdi
    syscall

cc_close_both:
    movq    $3, %rax
    movl    %r12d, %edi
    syscall
    movq    $3, %rax
    movl    %r13d, %edi
    syscall

cc_fail:
    addq    $16, %rsp
    popq    %r13
    popq    %r12
    ret

main:
    pushq   %rbp
    movq    %rsp, %rbp

    movq    %rdx, envp_save(%rip)

    xorq    %rdi, %rdi
    call    XOpenDisplay
    testq   %rax, %rax
    jz      fail_display
    movq    %rax, xdpy(%rip)

    movq    %rax, %rdi
    call    XDefaultScreen
    movq    %rax, %rsi
    movq    xdpy(%rip), %rdi
    call    XRootWindow
    movq    %rax, xroot(%rip)

    subq    $8, %rsp
    pushq   $CurrentTime
    pushq   $0
    pushq   $0
    movq    xdpy(%rip), %rdi
    movq    xroot(%rip), %rsi
    movq    $0, %rdx
    movq    $ButtonPressMask, %rcx
    movq    $GrabModeAsync, %r8
    movq    $GrabModeAsync, %r9
    call    XGrabPointer
    addq    $32, %rsp

    movq    xdpy(%rip), %rdi
    movq    xroot(%rip), %rsi
    movq    $0, %rdx
    movq    $KeyPressMask, %rcx
    movq    $GrabModeAsync, %r8
    movq    $CurrentTime, %r9
    call    XGrabKeyboard

    leaq    str_prompt(%rip), %rdi
    call    printz

event_loop:
    movq    xdpy(%rip), %rdi
    leaq    xev_buf(%rip), %rsi
    call    XNextEvent

    movl    xev_buf(%rip), %eax
    cmpl    $ButtonPress, %eax
    je      got_click
    cmpl    $KeyPress, %eax
    je      got_cancel
    jmp     event_loop

got_cancel:
    leaq    str_cancelled(%rip), %rdi
    call    printz
    jmp     cleanup

got_click:
    movslq  xev_buf+72(%rip), %rdx
    movslq  xev_buf+76(%rip), %rcx
    pushq   $ZPixmap
    pushq   $-1
    movq    xdpy(%rip), %rdi
    movq    xroot(%rip), %rsi
    movq    $1, %r8
    movq    $1, %r9
    call    XGetImage
    addq    $16, %rsp
    testq   %rax, %rax
    jz      cleanup
    movq    %rax, ximg(%rip)

    movq    %rax, %rdi
    xorq    %rsi, %rsi
    xorq    %rdx, %rdx
    call    XGetPixel
    movq    %rax, pixel_val(%rip)

    call    extract_rgb

    movq    ximg(%rip), %rdi
    call    XDestroyImage
    movq    $0, ximg(%rip)

    call    format_hex

    leaq    str_sp(%rip), %rdi
    call    printz

    movq    $1, %rdi
    leaq    clr_aqua(%rip), %rsi
    movq    $CLR_AQUA_LEN, %rdx
    call    print_raw

    leaq    outbuf(%rip), %rdi
    call    printz

    leaq    clr_reset_nl(%rip), %rdi
    call    printz

    call    clip_copy

    # write notification to FIFO (non-blocking)
    movq    $2, %rax
    leaq    path_fifo(%rip), %rdi
    movq    $0x801, %rsi           # O_WRONLY | O_NONBLOCK
    xorq    %rdx, %rdx
    syscall
    testq   %rax, %rax
    js      1f
    movq    %rax, %rdi
    leaq    outbuf(%rip), %rsi
    movq    $7, %rdx               # "#RRGGBB"
    movq    $1, %rax
    syscall
    movq    $3, %rax
    syscall
1:

    jmp     cleanup

fail_display:
    leaq    str_no_display(%rip), %rdi
    call    printz
    popq    %rbp
    movq    $60, %rax
    movq    $1, %rdi
    syscall

cleanup:
    movq    ximg(%rip), %rdi
    testq   %rdi, %rdi
    jz      1f
    call    XDestroyImage
    movq    $0, ximg(%rip)
1:
    movq    xdpy(%rip), %rdi
    testq   %rdi, %rdi
    jz      do_exit

    movq    %rdi, %r12
    movq    %r12, %rdi
    movq    $CurrentTime, %rsi
    call    XUngrabPointer

    movq    %r12, %rdi
    movq    $CurrentTime, %rsi
    call    XUngrabKeyboard

    movq    %r12, %rdi
    xorq    %rsi, %rsi
    call    XSync

    movq    %r12, %rdi
    call    XCloseDisplay

do_exit:
    popq    %rbp
    movq    $60, %rax
    xorq    %rdi, %rdi
    syscall
