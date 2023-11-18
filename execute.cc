#include "cpu.h"
#include "device.h"
#include "emulate.h"
#include <iostream>
#include <cassert>

using std::uint32_t;
using std::uint16_t;

#define EXHAUST0(mac, ...)			\
  mac(0 __VA_OPT__(,) __VA_ARGS__)		\
  mac(1 __VA_OPT__(,) __VA_ARGS__)		\
  mac(2 __VA_OPT__(,) __VA_ARGS__)		\
  mac(3 __VA_OPT__(,) __VA_ARGS__)		\
  mac(4 __VA_OPT__(,) __VA_ARGS__)		\
  mac(5 __VA_OPT__(,) __VA_ARGS__)		\
  mac(6 __VA_OPT__(,) __VA_ARGS__)		\
  mac(7 __VA_OPT__(,) __VA_ARGS__)

#define EXHAUST1(mac, ...)			\
  mac(0 __VA_OPT__(,) __VA_ARGS__)		\
  mac(1 __VA_OPT__(,) __VA_ARGS__)		\
  mac(2 __VA_OPT__(,) __VA_ARGS__)		\
  mac(3 __VA_OPT__(,) __VA_ARGS__)		\
  mac(4 __VA_OPT__(,) __VA_ARGS__)		\
  mac(5 __VA_OPT__(,) __VA_ARGS__)		\
  mac(6 __VA_OPT__(,) __VA_ARGS__)		\
  mac(7 __VA_OPT__(,) __VA_ARGS__)

#define EXHAUST2(mac, ...)			\
  mac(0 __VA_OPT__(,) __VA_ARGS__)		\
  mac(1 __VA_OPT__(,) __VA_ARGS__)		\
  mac(2 __VA_OPT__(,) __VA_ARGS__)		\
  mac(3 __VA_OPT__(,) __VA_ARGS__)		\
  mac(4 __VA_OPT__(,) __VA_ARGS__)		\
  mac(5 __VA_OPT__(,) __VA_ARGS__)		\
  mac(6 __VA_OPT__(,) __VA_ARGS__)		\
  mac(7 __VA_OPT__(,) __VA_ARGS__)

#define EXHAUST3(mac, ...)			\
  mac(0 __VA_OPT__(,) __VA_ARGS__)		\
  mac(1 __VA_OPT__(,) __VA_ARGS__)		\
  mac(2 __VA_OPT__(,) __VA_ARGS__)		\
  mac(3 __VA_OPT__(,) __VA_ARGS__)		\
  mac(4 __VA_OPT__(,) __VA_ARGS__)		\
  mac(5 __VA_OPT__(,) __VA_ARGS__)		\
  mac(6 __VA_OPT__(,) __VA_ARGS__)		\
  mac(7 __VA_OPT__(,) __VA_ARGS__)

#define EXHAUST4(mac, ...)			\
  mac(0 __VA_OPT__(,) __VA_ARGS__)		\
  mac(1 __VA_OPT__(,) __VA_ARGS__)		\
  mac(2 __VA_OPT__(,) __VA_ARGS__)		\
  mac(3 __VA_OPT__(,) __VA_ARGS__)		\
  mac(4 __VA_OPT__(,) __VA_ARGS__)		\
  mac(5 __VA_OPT__(,) __VA_ARGS__)		\
  mac(6 __VA_OPT__(,) __VA_ARGS__)		\
  mac(7 __VA_OPT__(,) __VA_ARGS__)

#define EXHAUST5(mac, ...)			\
  mac(0 __VA_OPT__(,) __VA_ARGS__)		\
  mac(1 __VA_OPT__(,) __VA_ARGS__)		\
  mac(2 __VA_OPT__(,) __VA_ARGS__)		\
  mac(3 __VA_OPT__(,) __VA_ARGS__)		\
  mac(4 __VA_OPT__(,) __VA_ARGS__)		\
  mac(5 __VA_OPT__(,) __VA_ARGS__)		\
  mac(6 __VA_OPT__(,) __VA_ARGS__)		\
  mac(7 __VA_OPT__(,) __VA_ARGS__)

#ifdef __GNUC__

#  define CASE(rd, rs1, rs2, label)			\
  labels[make_inst_noimm(OP_##label, rd, rs1, rs2)] =

#  define JUMP(label) &&label;

#else

#  define CASE(rd, rs1, rs2, label)			\
  case make_inst_noimm(OP_##label, rd, rs1, rs2):

#  define JUMP(label) goto label;

#endif

#define BINARY3_CASES(rd, rs1, rs2, label)		\
  CASE(rd, rs1, rs2, label)				\
  JUMP(label##rd##rs1##rs2)

#define BINARY2_CASES(rs1, rs2, label)			\
  EXHAUST0(BINARY3_CASES, rs1, rs2, label)

#define BINARY1_CASES(rs2, label)		\
  EXHAUST1(BINARY2_CASES, rs2, label)

#define BINARY0_CASES(label)			\
  EXHAUST2(BINARY1_CASES, label)

#define CASES_UNARY3(rs1, rs2, rd, label)	\
  CASE(rd, rs1, rs2, label)

#define CASES_UNARY2(rs2, rd, label)		\
  EXHAUST0(CASES_UNARY3, rs2, rd, label)

#define UNARY2_CASES(rd, rs2, label)		\
  CASES_UNARY2(rs2, rd, label)			\
  JUMP(label##rd##rs2)

#define UNARY1_CASES(rs2, label)		\
  EXHAUST1(UNARY2_CASES, rs2, label)

#define UNARY0_CASES(label)			\
  EXHAUST2(UNARY1_CASES, label)

#define CASES_NULLARY1(rd, label)		\
  EXHAUST1(CASES_UNARY2, rd, label)

#define NULLARY1_CASES(rd, label)		\
  CASES_NULLARY1(rd, label)			\
  JUMP(label##rd)

#define NULLARY0_CASES(label)			\
  EXHAUST2(NULLARY1_CASES, label)

#define CASES_NORES_BINARY2(rs1, rs2, label)	\
  EXHAUST0(CASE, rs1, rs2, label)

#define NORES_BINARY2_CASES(rs1, rs2, label)	\
  CASES_NORES_BINARY2(rs1, rs2, label)		\
  JUMP(label##rs1##rs2)

#define NORES_BINARY1_CASES(rs2, label)		\
  EXHAUST1(NORES_BINARY2_CASES, rs2, label)

#define NORES_BINARY0_CASES(label)		\
  EXHAUST2(NORES_BINARY1_CASES, label)

#define CASES_NORES_UNARY1(rs2, label)		\
  EXHAUST1(CASES_NORES_BINARY2, rs2, label)

#define NORES_UNARY1_CASES(rs2, label)		\
  CASES_NORES_UNARY1(rs2, label)		\
  JUMP(label##rs2)

#define NORES_UNARY0_CASES(label)		\
  EXHAUST2(NORES_UNARY1_CASES, label)

#define CASES_NORES_NULLARY0(label)		\
  EXHAUST2(CASES_NORES_UNARY1, label)

#define NORES_NULLARY0_CASES(label)		\
  CASES_NORES_NULLARY0(label)			\
  JUMP(label)

#define CASES_NOT3(rs2, rd, rs1)		\
  CASE(rd, rs1, rs2, NOT)

#define CASES_NOT2(rd, rs1)			\
  EXHAUST0(CASES_NOT3, rd, rs1)

#define NOT2_CASES(rd, rs1)			\
  CASES_NOT2(rd, rs1)				\
  JUMP(NOT##rd##rs1)

#define NOT1_CASES(rs1)				\
  EXHAUST1(NOT2_CASES, rs1)

#define NOT0_CASES()				\
  EXHAUST2(NOT1_CASES)

#define ALL_CASES				\
    BINARY0_CASES(ADD)				\
    BINARY0_CASES(SUB)				\
    BINARY0_CASES(AND)				\
    BINARY0_CASES(OR)				\
    BINARY0_CASES(XOR)				\
    NOT0_CASES()				\
    UNARY0_CASES(LOAD)				\
    UNARY0_CASES(STORE)				\
    NORES_NULLARY0_CASES(JUMP)			\
    NORES_UNARY0_CASES(BRANCH)			\
    NORES_BINARY0_CASES(CMP)			\
    NORES_UNARY0_CASES(BEQ)			\
    NORES_UNARY0_CASES(BNE)			\
    NORES_UNARY0_CASES(BLT)			\
    NORES_UNARY0_CASES(BGT)			\
    NULLARY0_CASES(LOADI)			\
    NULLARY0_CASES(CALL)			\
    NULLARY0_CASES(LOADI16)			\
    NULLARY0_CASES(LOADI16H)

#ifdef __GNUC__

#  define GOTO_NEXT_INST			\
  if(inst > make_inst(OPCODES, 7, 7, 7, -1))	\
    goto invalid;				\
  goto *labels[inst >> 17];

#else

#  define GOTO_NEXT_INST			\
  switch(inst >> 17) {				\
    ALL_CASES					\
  default:					\
    goto invalid;				\
  }

#endif

#define FIRST_INST				\
  inst = get(lrc, lrb, lrl, pc);		\
  maybe_single_step(single_step, inst, REGS);	\
  GOTO_NEXT_INST

#define NEXT_INST				\
  pc += 4;					\
  FIRST_INST

[[gnu::always_inline]]
static inline uint32_t get(std::uint32_t * lrc, std::uint32_t lrb,
			   std::uint32_t lrl, uint32_t addr) {
  if(word_in_range(addr, lrb, lrl) && lrc)
    [[likely]] return get_word_raw(lrc, lrl, addr - lrb);
  else return get_word(addr);
}

#define BINARY3(rd, rs1, rs2, label, op)	\
  label##rd##rs1##rs2:				\
  r##rd = r##rs1 op r##rs2;			\
  NEXT_INST

#define BINARY2(rs1, rs2, label, op)		\
  EXHAUST3(BINARY3, rs1, rs2, label, op)

#define BINARY1(rs2, label, op)			\
  EXHAUST4(BINARY2, rs2, label, op)

#define BINARY0(label, op)			\
  EXHAUST5(BINARY1, label, op)

#define NOT2(rd, rs1)				\
  NOT##rd##rs1:					\
  r##rd = ~r##rs1;				\
  NEXT_INST

#define NOT1(rs1)				\
  EXHAUST3(NOT2, rs1)

#define NOT0()					\
  EXHAUST4(NOT1)

#define LOAD2(rd, rs2)				\
  LOAD##rd##rs2:				\
  r##rd = get(lrc, lrb, lrl, r##rs2 + imm);	\
  NEXT_INST

#define LOAD1(rs2)				\
  EXHAUST3(LOAD2, rs2)

#define LOAD0()					\
  EXHAUST4(LOAD1)

#define STORE2(rd, rs2)							\
  STORE##rd##rs2:							\
  { const uint32_t dest = r##rs2 + imm;					\
    if(word_in_range(dest, lmb, lml) && lmc)				\
      [[likely]] set_word_raw(lmc, lml, dest - lmb, r##rd);		\
    else set_word(dest, r##rd);						\
  }									\
  NEXT_INST

#define STORE1(rs2)				\
  EXHAUST3(STORE2, rs2)

#define STORE0()				\
  EXHAUST4(STORE1)

#define BRANCH1(rs2)				\
  BRANCH##rs2:					\
  if(!r##rs2) pc += imm;			\
  NEXT_INST

#define BRANCH0()				\
  EXHAUST3(BRANCH1)

#define CMP2(rs1, rs2)							\
  CMP##rs1##rs2:							\
  Z = r##rs1 == r##rs2;							\
  N = static_cast<int32_t>(r##rs1) < static_cast<int32_t>(r##rs2);	\
  cmp = true;								\
  NEXT_INST

#define CMP1(rs2)				\
  EXHAUST3(CMP2, rs2)

#define CMP0()					\
  EXHAUST4(CMP1)

#define BCC1(rs2, label, cond, pred)		\
  label##rs2:					\
  if(cmp ? (cond) : r##rs2 pred)		\
    pc += imm;					\
  NEXT_INST

#define BCC0(label, cond, pred)			\
  EXHAUST3(BCC1, label, cond, pred)

#define BGT1(rs2)				\
  BGT##rs2:					\
  if(cmp ? !N && !Z : !(r##rs2 & 0x80000000))	\
    pc += imm;					\
  NEXT_INST

#define BGT0()					\
  EXHAUST3(BGT1)

#define LOADI1(rd)				\
  LOADI##rd:					\
  r##rd = inst_loadi_imm(inst);			\
  NEXT_INST

#define LOADI0()				\
  EXHAUST3(LOADI1)

#define CALL1(rd)				\
  CALL##rd:					\
  pc = r##rd - 4;				\
  NEXT_INST

#define CALL0()					\
  EXHAUST3(CALL1)

#define MEMCPY_COND (CHAR_BIT == 8 && sizeof(uint32_t) == 2*sizeof(uint16_t))

#define LOADI16HW1(rd, HW, mask, lop)					\
  LOADI16##HW##rd:							\
  r##rd &= (mask);							\
  r##rd |= imm lop;							\
  NEXT_INST

#define LOADI16HW0(HW, mask, lop)			\
  EXHAUST3(LOADI16HW1, HW, mask, lop)

void CPU::execute() {
  bool single_step = false;
  array_device * const lr = largest_readable;
  std::uint32_t * const lrc = lr ? lr->get_contents() : nullptr;
  const std::uint32_t lrb = lr ? lr->get_base() : 0;
  const std::uint32_t lrl = lr ? lr->get_limit() : 0;
  memory * const lm = largest_memory;
  std::uint32_t * const lmc = lm ? lm->get_contents() : nullptr;
  const std::uint32_t lmb = lm ? lm->get_base() : 0;
  const std::uint32_t lml = lm ? lm->get_limit() : 0;

#ifdef __GNUC__
  void * labels[(OPCODES + 1) << 9];
  ALL_CASES;
#endif

  uint32_t inst;
#define imm (inst_imm(inst))
  uint32_t r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0;
  FIRST_INST;

  BINARY0(ADD, +);
  BINARY0(SUB, -);
  BINARY0(AND, &);
  BINARY0(OR, |);
  BINARY0(XOR, ^);
  NOT0();
  LOAD0();
  STORE0();

 JUMP:
  pc += imm;
  NEXT_INST;

  BRANCH0();
  CMP0();
  BCC0(BEQ, Z, == 0);
  BCC0(BNE, !Z, != 0);
  BCC0(BLT, N, & 0x80000000);
  BGT0();
  LOADI0();
  CALL0();
  LOADI16HW0(, 0xFFFF0000, & 0xFFFF);
  LOADI16HW0(H, 0xFFFF, << 16);
 invalid:
  std::cerr << "invalid opcode\n";
  exit(-2);
#undef imm
}
