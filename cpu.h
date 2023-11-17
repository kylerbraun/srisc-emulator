// -*- C++ -*-
#ifndef CPU_H_
#define CPU_H_
#include <vector>
#include <cstdint>

class CPU {
  struct breakpoint {
    int num;
    std::uint32_t addr;
  };

  std::uint32_t regs[8] = { 0 };
  std::uint32_t pc = 0;
  bool Z = false, N = false, cmp = false;
  std::vector<breakpoint> breakpoints;
  int next_breakpoint = 1;

  void check_breakpoint(bool&, std::vector<breakpoint>::const_iterator&);

  [[gnu::always_inline]]
  void check_breakpoints(bool& single_step) {
    for(auto it = breakpoints.cbegin(); it != breakpoints.cend();) {
      [[unlikely]]
      check_breakpoint(single_step, it);
    }
  }

  void single_step(bool&, uint32_t);

  [[gnu::always_inline]]
  void maybe_single_step(bool& single_step, uint32_t inst) {
    check_breakpoints(single_step);
    if(single_step) this->single_step(single_step, inst);
  }

public:
  void add_breakpoint(std::uint32_t);
  void execute();
};

#endif
