%include "simple_risc.inc"

	loadi r1, 1
	loadi r5, 0FFh
loop:
	loadl r2, r0, hello
	and r2, r2, r5
	branch r2, $
finish:
	load r4, r3, -4
	and r4, r4, r1
	branch r4, finish

	store r2, r3, -4
	add r0, r0, r1
	jump loop

hello:
	db "Hello, world!",10,0
