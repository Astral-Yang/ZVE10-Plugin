/* ZV-E10 原厂 WidgetSystem::dispatchMouseRelease / dispatchMousePress Thumb-2 复原跳板 */
.syntax unified
.thumb
.section .text

.balign 4
.global zvp_mtf_orig_release
.hidden zvp_mtf_orig_release
.thumb_func
zvp_mtf_orig_release:
    push.w {r4, r5, r6, r7, r8, sb, sl, lr}
    sub sp, #0x20
    mov.w r8, #0x14
    movs r2, #1
    ldr.w pc, [pc, #0]
.global zvp_mtf_rel_resume_slot
.hidden zvp_mtf_rel_resume_slot
zvp_mtf_rel_resume_slot:
    .word 0

.balign 4
.global zvp_mtf_orig_press
.hidden zvp_mtf_orig_press
.thumb_func
zvp_mtf_orig_press:
    push.w {r4, r5, r6, r7, r8, sb, sl, lr}
    sub sp, #0x20
    mov.w sb, #0x14
    movs r2, #1
    ldr.w pc, [pc, #0]
.global zvp_mtf_prs_resume_slot
.hidden zvp_mtf_prs_resume_slot
zvp_mtf_prs_resume_slot:
    .word 0

.section .note.GNU-stack,"",%progbits
