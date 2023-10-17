%include "simple_risc.inc"

	loadi r6, 1
	loadi r5, 100h
	loadi r4, 0FFh
	loadi r3, 200h
read:
	load r0, r7, -8
	and r1, r0, r5
	branch r1, read
	and r1, r0, r3
	bne r1, $

	and r0, r0, r4
write:
	load r1, r7, -4
	and r1, r1, r6
	branch r1, write
	store r0, r7, -4
	jump read
