// -*- C++ -*-
#ifndef CPU_H_
#define CPU_H_
#include <vector>
#include <cstdint>

#define REGS r0, r1, r2, r3, r4, r5, r6, r7
#define REGS_PARAMS \
  std::uint32_t r0, std::uint32_t r1, std::uint32_t r2, std::uint32_t r3, \
    std::uint32_t r4, std::uint32_t r5, std::uint32_t r6, std::uint32_t r7

/* Represents the CPU state.  Note that the general purpose registers and
   program counter are not part of this -- they are instead local variables of
   execute, for efficiency. */
class CPU {
  struct breakpoint {
    int num;
    std::uint32_t addr;
  };

  /* The Z and N flags are the zero and negative flags modified by the cmp
     instruction.  The cmp flag indicates whether a cmp instruction has been
     executed yet.  This is necessary because some branch instructions behave
     differently depending on whether a cmp instruction has been executed. */
  bool Z = false, N = false, cmp = false;
  std::vector<breakpoint> breakpoints;
  int next_breakpoint = 1;

  /* Checks whether the breakpoint pointed to by the given iterator has just
     been reached.  If so, sets single_step to true and prints a breakpoint
     message.  In any case, the iterator is incremented just before returning.
  */
  void check_breakpoint(bool& single_step, std::uint32_t pc,
			std::vector<breakpoint>::const_iterator&);

  /* Checks whether any breakpoint has just been reached.  If so, sets
     single_step to true and prints a breakpoint message for each breakpoint
     that has just been reached. */
  [[gnu::always_inline]]
  void check_breakpoints(bool& single_step, std::uint32_t pc) {
    for(auto it = breakpoints.cbegin(); it != breakpoints.cend();) {
      [[unlikely]]
      check_breakpoint(single_step, pc, it);
    }
  }

  /* Executes the given instruction in single step mode.  Afterwards,
     single_step is set to indicate whether the next instruction should be
     executed in single step mode. */
  void single_step(bool& single_step, std::uint32_t pc, std::uint32_t inst,
		   REGS_PARAMS);

  /* If a breakpoint has just been reached, executes the given instruction in
     single step mode.  Otherwise, no execution is performed.  In any case,
     single_step is set to indicate whether the next instruction should also be
     executed in single step mode. */
  [[gnu::always_inline]]
  void maybe_single_step(bool& single_step, std::uint32_t pc,
			 std::uint32_t inst, REGS_PARAMS) {
    check_breakpoints(single_step, pc);
    if(single_step) this->single_step(single_step, pc, inst, REGS);
  }

public:
  void add_breakpoint(std::uint32_t);
  // Executes instructions with all registers initially set to 0.
  void execute();
};

#endif
