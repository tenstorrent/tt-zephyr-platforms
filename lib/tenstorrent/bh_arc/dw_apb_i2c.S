.global I2CWriteBytes_ASM
I2CWriteBytes_ASM:
	enter_s [r13-r19,fp,blink]
	sub_s   sp,sp,0x4
#ifdef CONFIG_NOP_HACK
	nop
#endif
	sth     r1,[fp,-4]
	add     r17,r2,r4
	mov_s   r16,r0
	add     r0,r17,0x3
	bmskn   r0,r0,0x1
	mov_s   r18,sp
	sub     sp,sp,r0
	sub2    r1,fp,0x1
	mov_s   r0,sp
	mov_s   r14,r3
	mov_s   r13,r2
	mov_s   r15,r4
	bl      memcpy
	mov_s   r19,sp
	breq_s  r14,0,16        ;10010304 <I2CWriteBytes+0x3c>
	mov_s   r2,r15
	mov_s   r1,r14
	add     r0,sp,r13
	bl      memcpy
	mov_s   r4,0
	mov_s   r3,0
	mov_s   r2,r17
	mov_s   r1,r19
	mov_s   r0,r16
	bl      I2CTransaction
	mov_s   sp,r18
	leave_s [r13-r19,fp,blink,pcl]
