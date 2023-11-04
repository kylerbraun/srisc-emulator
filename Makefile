CFLAGS =
CXXFLAGS =
CC = gcc
CXX = g++

all: disasm emulate

emulate: emulate.o print.o
	$(CXX) emulate.o print.o -o emulate

disasm: disasm.o print.o
	$(CC) disasm.o print.o -o disasm

print.o: print.c emulate.h
	$(CC) $(CFLAGS) -c -Wall -Wextra -std=c11 print.c -o print.o

disasm.o: disasm.c emulate.h
	$(CC) $(CFLAGS) -c -Wall -Wextra -std=c11 disasm.c -o disasm.o

emulate.o: emulate.cc emulate.h
	$(CXX) $(CXXFLAGS) -c -Wall -Wextra -std=c++20 emulate.cc -o emulate.o

clean:
	rm -f emulate.o print.o disasm.o emulate disasm
