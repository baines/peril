
# TODO: floating point args
#       they go in xmm0 - xmm7

.global ext_call
ext_call:
    sub $8, %rsp

    mov %rdi, %r10 # func
    mov %rsi, %r11 # atom list

    test %r11, %r11
    jz .perform
    mov 8(%r11), %rdi
    mov 16(%r11), %r11

    test %r11, %r11
    jz .perform
    mov 8(%r11), %rsi
    mov 16(%r11), %r11

    test %r11, %r11
    jz .perform
    mov 8(%r11), %rdx
    mov 16(%r11), %r11

    test %r11, %r11
    jz .perform
    mov 8(%r11), %rcx
    mov 16(%r11), %r11

    test %r11, %r11
    jz .perform
    mov 8(%r11), %r8
    mov 16(%r11), %r11

    test %r11, %r11
    jz .perform
    mov 8(%r11), %r9
    mov 16(%r11), %r11

# TODO: if there are more args they get pushed on the stack in reverse order

.perform:
    xor %rax, %rax
    call *%r10

    add $8, %rsp

    ret

