#include "cpu.h"
#include "device.h"
#include "emulate.h"
#include <unistd.h>
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

void CPU::single_step(bool& single_step, uint32_t inst, REGS_PARAMS) {
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
    if(cmd.size() == 2 && cmd[0] == 'r' && (ri = cmd[1] - '0') < 8) {
      switch(cmd[1]) {
      case '0':
	print_num(r0);
	break;
      case '1':
	print_num(r1);
	break;
      case '2':
	print_num(r2);
	break;
      case '3':
	print_num(r3);
	break;
      case '4':
	print_num(r4);
	break;
      case '5':
	print_num(r5);
	break;
      case '6':
	print_num(r6);
	break;
      case '7':
	print_num(r7);
	break;
      default:
	assert(false);
      }
    }

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
