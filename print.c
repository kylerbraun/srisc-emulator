#include "emulate.h"
#include <stdio.h>
#include <stdbool.h>

static const char * const ops[] = {
  "add", "sub", "and", "or", "xor", "not", "load", "store", "jump", "branch",
  "cmp", "invalid", "beq", "bne", "blt", "bgt", "loadi", "call", "loadi16",
  "loadi16h"
};

static inline const char * op_name(enum opcode opcode) {
  if(opcode > sizeof(ops)/sizeof(char*))
    return "invalid";
  return ops[opcode];
}

void print_inst(uint32_t inst, FILE * fp) {
  const enum opcode opcode = inst_opcode(inst);
  const char * const op = op_name(opcode);
  const int rd = inst_rd(inst);
  const int rs1 = inst_rs1(inst);
  const int rs2 = inst_rs2(inst);
  const long imm = (int32_t)inst_imm(inst);
  switch(opcode) {
  case OP_ADD:
  case OP_SUB:
  case OP_AND:
  case OP_OR:
  case OP_XOR:
    fprintf(fp, "%s r%d, r%d, r%d\n", op, rd, rs1, rs2);
    break;
  case OP_NOT:
    fprintf(fp, "%s r%d, r%d\n", op, rd, rs1);
    break;
  case OP_LOAD:
  case OP_STORE:
    fprintf(fp, "%s r%d, r%d, %ld\n", op, rd, rs2, imm);
    break;
  case OP_JUMP:
    fprintf(fp, "%s %ld\n", op, imm);
    break;
  case OP_CMP:
    fprintf(fp, "%s r%d, r%d\n", op, rs1, rs2);
    break;
  case OP_BRANCH:
  case OP_BEQ:
  case OP_BNE:
  case OP_BLT:
  case OP_BGT:
    fprintf(fp, "%s r%d, %ld\n", op, rs2, imm);
    break;
  case OP_LOADI:
    { const long loadi_imm = (int32_t)inst_loadi_imm(inst);
      fprintf(fp, "loadi r%d, %ld\n", rd, loadi_imm);
    }
    break;
  case OP_CALL:
    if(rs1 != 0 || rs2 != 0 || imm != 0)
      puts("invalid");
    else fprintf(fp, "call r%d\n", rd);
    break;
  case OP_LOADI16:
  case OP_LOADI16H:
    fprintf(fp, "%s r%d, %ld\n", op, rd, imm);
    break;
  default:
    fputs("invalid\n", fp);
    break;
  }
}
