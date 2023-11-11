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

public:
  void add_breakpoint(std::uint32_t);
  void execute();
};

#endif
