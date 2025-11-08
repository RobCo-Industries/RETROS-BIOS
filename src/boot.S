/* Boot code for ARM11 (RPi0/1) and Cortex-A7/A53 (RPi2/3) */

.section ".text.boot"

.global _start

_start:
    /* Check processor ID - only CPU 0 should continue */
    mrc p15, 0, r5, c0, c0, 5
    and r5, r5, #3
    cmp r5, #0
    bne halt_cpu

    /* Set up stack pointer */
    ldr sp, =_stack_top

    /* Clear BSS section */
    ldr r4, =__bss_start
    ldr r9, =__bss_end
    mov r5, #0
    mov r6, #0
    mov r7, #0
    mov r8, #0

bss_loop:
    cmp r4, r9
    bge bss_done
    stmia r4!, {r5-r8}
    b bss_loop

bss_done:
    /* Call kernel_main */
    ldr r3, =kernel_main
    blx r3

halt_cpu:
    /* Halt the CPU */
    wfi
    b halt_cpu
