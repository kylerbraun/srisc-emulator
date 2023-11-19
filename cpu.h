// -*- C++ -*-
#ifndef CPU_H_
#define CPU_H_
#include <vector>
#include <cstdint>

#define REGS r0, r1, r2, r3, r4, r5, r6, r7
#define REGS_PARAMS \
  std::uint32_t r0, std::uint32_t r1, std::uint32_t r2, std::uint32_t r3, \
    std::uint32_t r4, std::uint32_t r5, std::uint32_t r6, std::uint32_t r7

class CPU {
  struct breakpoint {
    int num;
    std::uint32_t addr;
  };

  bool Z = false, N = false, cmp = false;
  std::vector<breakpoint> breakpoints;
  int next_breakpoint = 1;

  void check_breakpoint(bool&, std::uint32_t,
			std::vector<breakpoint>::const_iterator&);

  [[gnu::always_inline]]
  void check_breakpoints(bool& single_step, std::uint32_t pc) {
    for(auto it = breakpoints.cbegin(); it != breakpoints.cend();) {
      [[unlikely]]
      check_breakpoint(single_step, pc, it);
    }
  }

  void single_step(bool&, std::uint32_t, std::uint32_t, REGS_PARAMS);

  [[gnu::always_inline]]
  void maybe_single_step(bool& single_step, std::uint32_t pc,
			 std::uint32_t inst, REGS_PARAMS) {
    check_breakpoints(single_step, pc);
    if(single_step) this->single_step(single_step, pc, inst, REGS);
  }

public:
  void add_breakpoint(std::uint32_t);
  void execute();
};

#endif
