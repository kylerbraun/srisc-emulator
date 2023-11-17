CFLAGS =
CXXFLAGS =
ifdef TARGET
  TARGET_PREFIX = $(TARGET)-
endif
CC = $(TARGET_PREFIX)gcc
CXX = $(TARGET_PREFIX)g++

all: disasm emulate

emulate: emulate.o cpu.o execute.o device.o print.o
	$(CXX) emulate.o cpu.o execute.o device.o print.o -o emulate

disasm: disasm.o print.o
	$(CC) disasm.o print.o -o disasm

print.o: print.c emulate.h
	$(CC) $(CFLAGS) -c -Wall -Wextra -std=c11 print.c -o print.o

disasm.o: disasm.c emulate.h
	$(CC) $(CFLAGS) -c -Wall -Wextra -std=c11 disasm.c -o disasm.o

device.o: device.cc device.h
	$(CXX) $(CXXFLAGS) -c -Wall -Wextra -std=c++20 device.cc -o device.o

cpu.o: cpu.cc cpu.h device.h emulate.h
	$(CXX) $(CXXFLAGS) -c -Wall -Wextra -std=c++20 cpu.cc -o cpu.o

execute.o: execute.cc cpu.h device.h emulate.h
	$(CXX) $(CXXFLAGS) -c -Wall -Wextra -Wno-tautological-compare -std=c++20 execute.cc -o execute.o

emulate.o: emulate.cc emulate.h cpu.h device.h
	$(CXX) $(CXXFLAGS) -c -Wall -Wextra -std=c++20 emulate.cc -o emulate.o

clean:
	rm -f emulate.o cpu.o execute.o device.o print.o disasm.o emulate disasm
