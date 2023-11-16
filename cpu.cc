#include "cpu.h"
#include "device.h"
#include "emulate.h"
#include <iostream>
#include <utility>
#include <algorithm>
#include <optional>
#include <string_view>
#include <span>
#include <array>
#include <cctype>
#include <cstddef>
#include <cassert>

using namespace std::literals::string_view_literals;
using std::uint32_t;

static std::size_t accept(std::span<char> buf) {
  assert(buf.size() > 0);
  std::size_t read = 0;
  int c;
  while(c = std::cin.get(), !std::cin.eof()) {
    switch(c) {
    case '\b':
    case 127:
      std::cout << "\b \b";
      if(read > 0) read--;
      continue;
    case '\n':
      std::cout << '\n';
      goto end;
    case '\t':
      continue;
    }
    if(read + 1 >= buf.size()) continue;
    std::cout << static_cast<char>(c);
    buf[read++] = c;
  }
 end:
  buf[read] = 0;
  return read;
}

static constexpr int command_line_length = 512;

class command_line {
  const std::array<char, command_line_length> line;
  const std::vector<std::pair<const char*, const char*>> tokens;

public:
  class argument {
    command_line& parent;
    const int n;

    explicit argument(command_line& parent, int n) : parent{parent}, n{n} {}

  public:
    operator std::string_view() const {
      return std::string_view{parent.tokens[n].first, parent.tokens[n].second};
    }

    std::optional<uint32_t> parse() const {
      char * end;
      const uint32_t res = strtoul(parent.tokens[n].first, &end, 0);
      if(end != parent.tokens[n].second)
	return std::nullopt;
      return res;
    }

    friend class command_line;
  };

  explicit command_line(std::istream& stream)
    : line{[&]() {
      std::array<char, command_line_length> res;
      if(isatty(0)) accept(std::span{res.data(), res.size()});
      else
	stream.getline(res.data(), res.size());
      res[res.size() - 1] = 0;
      stream.clear();
      return res;
    }()}, tokens{[&]() {
      std::vector<std::pair<const char*, const char*>> res;
      const auto line_end = std::find(line.cbegin(), line.cend(), 0);
      auto it = line.cbegin();
      const auto isspace = static_cast<int(*)(int)>(std::isspace);
      while(true) {
	it = std::find_if_not(it, line_end, isspace);
	if(it == line_end || !*it) return res;
	const char * const first = it;
	it = std::find_if(it, line_end, isspace);
	res.push_back(std::pair{first, it});
      }
    }()} {}

  std::optional<std::string_view> get_command() {
    if(tokens.empty())
      return std::nullopt;
    return static_cast<std::string_view>(argument{*this, 0});
  }

  std::optional<argument> get_arg(int n) {
    if(static_cast<std::size_t>(n + 1) >= tokens.size())
      return std::nullopt;
    return argument{*this, n + 1};
  }
};

static void print_num(uint32_t num) {
  std::cerr << "0x" << std::hex << num
	    << std::dec << " (" << num << ")\n";
}

void CPU::add_breakpoint(uint32_t addr) {
  breakpoints.push_back({ next_breakpoint++, addr });
}

void CPU::check_breakpoint(bool& single_step,
			   std::vector<breakpoint>::const_iterator& it) {
  if(pc == it->addr) {
    single_step = true;
    if(it->num == -1) {
      it = breakpoints.erase(it);
      return;
    }
    std::cerr << "breakpoint " << it->num
	      << " at 0x" << std::hex << it->addr << std::dec << '\n';
  }
  ++it;
}

void CPU::single_step(bool& single_step, uint32_t inst) {
  std::cerr << "0x" << std::hex << pc << std::dec << ": ";
  print_inst(inst, stderr);
  while(true) {
    std::cerr << "> ";
    command_line cmdline{std::cin};
    if(!cmdline.get_command()) continue;
    const auto mcmd = cmdline.get_command();
    if(!mcmd) continue;
    const auto& cmd = *mcmd;

    const auto get_num = [&](int n) -> std::optional<uint32_t> {
      const auto arg = cmdline.get_arg(n);
      if(!arg) {
	std::cerr << "not enough arguments\n";
	return std::nullopt;
      }
      const auto num = arg->parse();
      if(!num) {
	const std::string_view bad = *arg;
	std::cerr << "bad number: " << bad << '\n';
	return std::nullopt;
      }
      return num;
    };

    unsigned ri;
    bool byte, hword;
    if(cmd.size() == 2 && cmd[0] == 'r' && (ri = cmd[1] - '0') < 8)
      print_num(regs[ri]);

    else if((byte = cmd == "byte"sv) || (hword = cmd == "hword"sv)
	    || cmd == "word"sv) {
      const auto addr = get_num(0);
      if(!addr) continue;
      if(byte) print_num(get_byte(*addr));
      else print_num(get_word(*addr) & (hword ? 0xFFFF : 0xFFFFFFFF));
    }

    else if(cmd == "b"sv || cmd == "break"sv) {
      const auto addr = get_num(0);
      if(!addr) continue;
      add_breakpoint(*addr);
    }

    else if(cmd == "d"sv || cmd == "delete"sv) {
      const auto num = get_num(0);
      if(!num) continue;
      for(auto it = breakpoints.cbegin(); it != breakpoints.cend();
	  ++it) {
	if(static_cast<int>(*num) == it->num) {
	  breakpoints.erase(it);
	  break;
	}
      }
    }

    else if(cmd == "s"sv || cmd == "step"sv)
      break;

    else if(cmd == "n"sv || cmd == "next"sv) {
      breakpoints.push_back({ -1, pc + 4 });
      single_step = false;
      break;
    }

    else if(cmd == "c"sv || cmd == "continue"sv) {
      single_step = false;
      break;
    }

    else std::cerr << "unknown debugger command: " << cmd << '\n';
  }
}

void CPU::execute() {
  bool single_step = false;
  for(;; pc += 4) {
    const auto get = [&](uint32_t addr) {
      if(word_in_device_range(addr, largest_readable))
	return
	  largest_readable->get_word_raw(addr - largest_readable->get_base());
      else return get_word(addr);
    };

    const uint32_t inst = get(pc);

    maybe_single_step(single_step, inst);

    const enum opcode opcode = inst_opcode(inst);
    uint32_t * const rd = regs + inst_rd(inst);
    uint32_t * const rs1 = regs + inst_rs1(inst);
    uint32_t * const rs2 = regs + inst_rs2(inst);
    const uint32_t imm = inst_imm(inst);
    switch(opcode) {
    case OP_ADD:
      *rd = *rs1 + *rs2;
      break;
    case OP_SUB:
      *rd = *rs1 - *rs2;
      break;
    case OP_AND:
      *rd = *rs1 & *rs2;
      break;
    case OP_OR:
      *rd = *rs1 | *rs2;
      break;
    case OP_XOR:
      *rd = *rs1 ^ *rs2;
      break;
    case OP_NOT:
      *rd = ~*rs1;
      break;
    case OP_LOAD:
      *rd = get(*rs2 + imm);
      break;
    case OP_STORE:
      { const uint32_t dest = *rs2 + imm;
	if(word_in_device_range(dest, largest_memory))
	  largest_memory->set_word_raw(dest - largest_memory->get_base(),
				       *rd);
	else set_word(dest, *rd);
      }
      break;
    case OP_JUMP:
      pc += imm;
      break;
    case OP_BRANCH:
      if(!*rs2) pc += imm;
      break;
    case OP_CMP:
      Z = *rs1 == *rs2;
      N = static_cast<int32_t>(*rs1) < static_cast<int32_t>(*rs2);
      cmp = true;
      break;
    case OP_BEQ:
      if(cmp ? Z : *rs2 == 0)
	pc += imm;
      break;
    case OP_BNE:
      if(cmp ? !Z : *rs2 != 0)
	pc += imm;
      break;
    case OP_BLT:
      if(cmp ? N : *rs2 & 0x80000000)
	pc += imm;
      break;
    case OP_BGT:
      if(cmp ? !N && !Z : !(*rs2 & 0x80000000))
	pc += imm;
      break;
    case OP_LOADI:
      *rd = inst_loadi_imm(inst);
      break;
    case OP_CALL:
      if(inst_rs1(inst) != 0 || inst_rs2(inst) != 0 || imm != 0)
	goto invalid;
      pc = *rd - 4;
      break;
    case OP_LOADI16:
      *rd &= 0xFFFF0000;
      *rd |= imm & 0xFFFF;
      break;
    case OP_LOADI16H:
      *rd &= 0xFFFF;
      *rd |= imm << 16;
      break;
    default:
    invalid:
      fprintf(stderr, "invalid opcode\n");
      exit(-2);
    }
  }
}
