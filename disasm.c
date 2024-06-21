#include "emulate.h"
#include <stdio.h>
#include <stdbool.h>

// List of operation names, indexed by opcode.
static const char * const ops[] = {
  "add", "sub", "and", "or", "xor", "not", "load", "store", "jump", "branch",
  "cmp", "invalid", "beq", "bne", "blt", "bgt", "loadi", "call", "loadi16",
  "loadi16h"
};

// Returns the operation name for a given opcode.
static inline const char * op_name(enum opcode opcode) {
  if(opcode > sizeof(ops)/sizeof(char*))
    return "invalid";
  return ops[opcode];
}

/* Prints textual representations of all the instructions contain within in.
   See print_inst for the output format. */
static void disassemble(FILE * in) {
  while(true) {
    uint32_t inst = 0;
    for(int i = 0; i <= 3; i++) {
      const int c = getc(in);
      if(feof(in)) return;
      inst |= (c & 0xFF) << i*8;
    }
    print_inst(inst, stdout);
  }
}

int main(int argc, const char ** argv) {
  if(argc < 2) {
    fprintf(stderr, "not enough arguments\n");
    return -1;
  }
  FILE * in = fopen(argv[1], "rb");
  if(!in) {
    fprintf(stderr, "cannot open %s: ", argv[1]);
    perror("");
    return -2;
  }
  disassemble(in);
  return 0;
}
