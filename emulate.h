#ifndef EMULATE_H_
#define EMULATE_H_
#include <stdio.h>
#include <stdint.h>

enum opcode {
  OP_ADD = 0, OP_SUB = 1, OP_AND = 2, OP_OR = 3, OP_XOR = 4, OP_NOT = 5,
  OP_LOAD = 6, OP_STORE = 7, OP_JUMP = 8, OP_BRANCH = 9, OP_CMP = 10,
  OP_BEQ = 12, OP_BNE = 13, OP_BLT = 14, OP_BGT = 15, OP_LOADI = 16,
  OP_CALL = 17, OP_LOADI16 = 18, OP_LOADI16H = 19, OPCODES = 19
};

static inline enum opcode inst_opcode(uint32_t inst) {
  return (enum opcode)(inst >> 26);
}

static inline int inst_rd(uint32_t inst) {
  return inst >> 23 & 0x7;
}

static inline int inst_rs1(uint32_t inst) {
  return inst >> 20 & 0x7;
}

static inline int inst_rs2(uint32_t inst) {
  return inst >> 17 & 0x7;
}

static inline uint32_t inst_imm(uint32_t inst) {
  uint32_t res = inst & 0xFFFF;
  if(inst & 0x10000) res |= 0xFFFF0000;
  return res;
}

static inline uint32_t inst_loadi_imm(uint32_t inst) {
  uint32_t res = inst & 0x3FFFFF;
  if(inst & 0x400000) res |= 0xFFC00000;
  return res;
}

#ifdef __cplusplus
constexpr
#endif
static inline uint32_t make_inst_noimm(enum opcode opcode,
				       int rd, int rs1, int rs2) {
  return (uint32_t)opcode << 9 |
    (uint32_t)rd << 6 | (uint32_t)rs1 << 3 | (uint32_t)rs2;
}

static inline uint32_t make_inst(enum opcode opcode, int rd, int rs1, int rs2,
				 uint32_t imm) {
  return make_inst_noimm(opcode, rd, rs1, rs2) << 17 | (imm & 0x1FFFF);
}

static inline uint32_t make_loadi_inst(int rd, uint32_t imm) {
  return (imm & 0x7FFFFF) | (uint32_t)rd << 23 | (uint32_t)0x40000000;
}

static inline void layout_inst(uint8_t * dest, const uint32_t inst) {
  dest[0] = inst & 0xFF;
  dest[1] = inst >> 8 & 0xFF;
  dest[2] = inst >> 16 & 0xFF;
  dest[3] = inst >> 24;
}

#ifdef __cplusplus
extern "C"
#endif
void print_inst(uint32_t, FILE*);

#endif
