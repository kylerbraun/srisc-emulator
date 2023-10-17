%define r0 0
%define r1 1
%define r2 2
%define r3 3
%define r4 4
%define r5 5
%define r6 6
%define r7 7

%define mod32(x) ((x) < 0 ? (1 << 32) + (x) : (x))

%macro instruction 5
	dd (mod32(%5) & 1FFFFh) | %4 << 17 | %3 << 20 | %2 << 23 | %1 << 26
%endmacro

%macro add 3
	instruction 0, %{1:3}, 0
%endmacro

%macro sub 3
	instruction 1, %{1:3}, 0
%endmacro

%macro and 3
	instruction 2, %{1:3}, 0
%endmacro

%macro or 3
	instruction 3, %{1:3}, 0
%endmacro

%macro xor 3
	instruction 4, %{2:3}, 0
%endmacro

%macro not 2
	instruction 5, %{1:2}, 0, 0
%endmacro

%macro load 3
	instruction 6, %1, r0, %{2:3}
%endmacro

%macro loadl 3
	load %1, %2, %3 - $$
%endmacro

%macro store 3
	instruction 7, %1, r0, %{2:3}
%endmacro

%macro cmp 2
	instruction 10, r0, %{1:2}, 0
%endmacro

%macro call 1
	instruction 17, r0, r0, %1, 0
%endmacro

%macro relinst 3
	instruction %1, r0, r0, %2, (%3 - $$) - ($ - $$) - 4
%endmacro relinst

%macro jump 1
	relinst 8, r0, %1
%endmacro

%macro branch 2
	relinst 9, %{1:2}
%endmacro

%macro beq 2
	relinst 12, %{1:2}
%endmacro

%macro bne 2
	relinst 13, %{1:2}
%endmacro

%macro blt 2
	relinst 14, %{1:2}
%endmacro

%macro bgt 2
	relinst 15, %{1:2}
%endmacro

%macro loadi 2
	dd (mod32(%2) & 0x7FFFFF) | (%1 << 23) | 0x40000000
%endmacro
