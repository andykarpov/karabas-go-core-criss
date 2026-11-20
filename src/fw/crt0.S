/* Helper firmware start (from core-doom crt0_hw.S). The CPU is reset to 0x10000000 at every start:
   the first one after the ROM load (.data as loaded) and each one after the Reset item (.data as
   the last run left it). kg_boot_magic (in .data) tells them apart: BOOT_INIT as loaded -> save
   .data to __data_save; BOOT_RUN -> restore .data from there. Then: BOOT_RUN, clear .bss, main. */
    .equ BOOT_INIT, 0x600d0001
    .equ BOOT_RUN,  0x600d0002

    .section .data
    .align 2
    .global kg_boot_magic
kg_boot_magic:
    .word BOOT_INIT

    .section .text.start
    .global _start
_start:
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop
    la      sp, __stack_top
    la      a0, __data_start
    la      a1, __data_end
    la      a2, __data_save
    la      t0, kg_boot_magic
    lw      t1, 0(t0)
    li      t2, BOOT_RUN
    beq     t1, t2, restore
1:  bgeu    a0, a1, 3f            /* save: .data -> __data_save */
    lw      t3, 0(a0)
    sw      t3, 0(a2)
    addi    a0, a0, 4
    addi    a2, a2, 4
    j       1b
restore:
2:  bgeu    a0, a1, 3f            /* restore: __data_save -> .data */
    lw      t3, 0(a2)
    sw      t3, 0(a0)
    addi    a0, a0, 4
    addi    a2, a2, 4
    j       2b
3:  la      t0, kg_boot_magic
    li      t1, BOOT_RUN
    sw      t1, 0(t0)
    la      a0, __bss_start
    la      a1, __bss_end
4:  bgeu    a0, a1, 5f
    sw      zero, 0(a0)
    addi    a0, a0, 4
    j       4b
5:
    li      a0, 0
    li      a1, 0
    call    main
6:  j       6b
