#define _POSIX_C_SOURCE 200809L
#include "emulate.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <getopt.h>
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <charconv>
#include <system_error>
#include <optional>
#include <variant>
#include <array>
#include <string_view>
#include <span>
#include <vector>
#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <chrono>
#include <stdexcept>
#include <type_traits>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <cassert>
#include <cstdint>
#include <cstddef>

using namespace std::literals::string_view_literals;
using std::uint8_t;
using std::uint32_t;

class device {
  uint32_t base;
  uint32_t lim;

public:
  device(uint32_t base, uint32_t lim);
  device(const device&) = delete;
  device(device&&) = delete;

protected:
  ~device() {}

public:
  device& operator=(const device&) = delete;
  device& operator=(device&&) = delete;

  uint32_t get_base() { return base; }
  uint32_t get_limit() { return lim; }

private:
  virtual uint8_t get_byte_impl(uint32_t) = 0;
  virtual void set_byte_impl(uint32_t, uint8_t) = 0;

public:
  uint8_t get_byte(uint32_t off) {
    if(off > lim) return 0;
    return get_byte_impl(off);
  }

  void set_byte(uint32_t off, uint8_t byte) {
    if(off > lim) return;
    set_byte_impl(off, byte);
  }

private:
  virtual uint32_t get_word_impl(uint32_t off) {
    uint32_t res = 0;
    res |= get_byte(off);
    res |= get_byte(off + 1) << 8;
    res |= get_byte(off + 2) << 16;
    res |= get_byte(off + 3) << 24;
    return res;
  }

  virtual void set_word_impl(uint32_t off, uint32_t word) {
    set_byte(off, word & 0xFF);
    set_byte(off + 1, word >> 8 & 0xFF);
    set_byte(off + 2, word >> 16 & 0xFF);
    set_byte(off + 3, word >> 24 & 0xFF);
  }

  uint32_t clean_word(uint32_t off, uint32_t word) {
    if(off > lim && off <= (uint32_t)-4) return 0;
    if(lim - off < 3) word &= (uint32_t)0xFFFFFFFF >> (3 - (lim - off))*8;
    if(off > (uint32_t)-4) word &= (uint32_t)0xFFFFFFFF << (-off)*8;
    return word;
  }

public:
  uint32_t get_word(uint32_t off) {
    return clean_word(off, get_word_impl(off));
  }

  void set_word(uint32_t off, uint32_t word) {
    set_word_impl(off, clean_word(off, word));
  }
};

template<typename Entry> using NLE =
  std::variant<device*, std::unique_ptr<std::array<Entry, 1024>>>;
using L2E = NLE<device*>;
using L3E = NLE<L2E>;

template<typename T> struct shift;

template<> struct shift<std::array<device*, 1024>> {
  static constexpr int value = 2;
};

template<typename Entry> struct shift<std::array<NLE<Entry>, 1024>> {
  static constexpr int value = shift<std::array<Entry, 1024>>::value + 10;
};

static std::array<L3E, 1024> devtab;

template<typename Entry>
static void set_devent(NLE<Entry>&, uint32_t, uint32_t, device*);

static void set_devtab(std::array<device*, 1024>& tab, uint32_t base,
		       uint32_t lim, device * dev) {
  const uint32_t si = base >> 2;
  const uint32_t li = lim >> 2;
  for(long long i = 0; i <= li - si; i++)
    tab[si + i] = dev;
}

template<typename Entry>
static void set_devtab(std::array<NLE<Entry>, 1024>& tab, uint32_t base,
		       uint32_t lim, device * dev) {
  const int cs = shift<std::remove_cvref_t<decltype(tab)>>::value;
  const uint32_t mask = ((uint32_t)1 << cs) - 1;
  const uint32_t si = base >> cs;
  const uint32_t li = lim >> cs;
  const uint32_t os = base & mask;
  const uint32_t ol = lim & mask;
  const bool zero = si == li && os <= ol;
  if(!zero)
    for(long long i = os != 0; i < (long long)li - si + (ol == mask); i++)
      tab[si + i] = dev;
  if(os != 0) set_devent(tab[si], os, zero ? ol : mask, dev);
  if(ol != mask && (os == 0 || !zero)) set_devent(tab[li], 0, ol, dev);
}

template<typename Entry> static void set_devent(NLE<Entry>& ent, uint32_t base,
						uint32_t lim, device * dev) {
  const auto ptr = std::visit([&](const auto& val) {
    if constexpr(std::is_same_v<decltype(val), device* const&>) {
      auto ptr = std::make_unique<std::array<Entry, 1024>>();
      std::fill(ptr->begin(), ptr->end(), val);
      const auto res = ptr.get();
      ent = std::move(ptr);
      return res;
    }
    else return val.get();
  }, ent);
  set_devtab(*ptr, base, lim, dev);
}

device::device(uint32_t base, uint32_t lim) : base{base}, lim{lim} {
  set_devtab(devtab, base, base + lim, this);
}

static inline bool byte_in_device_range(uint32_t addr, device * dev) {
  return addr >= dev->get_base() && addr - dev->get_base() < dev->get_limit();
}

static inline bool word_in_device_range(uint32_t addr, device * dev) {
  return dev && addr >= dev->get_base()
    && addr + 3 - dev->get_base() < dev->get_limit();
}

template<typename T> class read_only_device : public T {
protected:
  using T::T;

private:
  void set_byte_impl(uint32_t, uint8_t) override {}
  void set_word_impl(uint32_t, uint32_t) override {}
};

template<auto Conv> class fn_array_device : public device {
  uint32_t * const contents;

protected:
  fn_array_device(uint32_t * contents, uint32_t base, uint32_t lim)
    : device{base, lim}, contents{contents} {}

private:
  uint32_t& get_aligned_ref(uint32_t off) {
    return contents[off >> 2];
  }

  char * get_offset(uint32_t off) {
    return reinterpret_cast<char*>(contents) + off;
  }

  /* GCC is unable to optimize out the bitshift in get_aligned_ref even if we
     first make sure the offset is aligned.  We therefore need this function to
     optimize out the bitshift. */
  uint32_t& get_exact_ref(uint32_t off) {
    if constexpr(sizeof(uint32_t) == 4)
      return *reinterpret_cast<uint32_t*>(get_offset(off));
    else return get_aligned_ref(off);
  }

  uint32_t get_alignedl(uint32_t off) {
    if(off >> 2 <= get_limit() >> 2)
      return Conv(get_aligned_ref(off));
    else return 0;
  }

  void set_alignedl(uint32_t off, uint32_t word) {
    if(off >> 2 <= get_limit() >> 2)
      get_aligned_ref(off) = Conv(word);
  }

public:
  uint32_t get_word_raw(uint32_t off) {
    if constexpr(std::endian::native == std::endian::little &&
		 sizeof(uint32_t) == 4 && CHAR_BIT == 8) {
      uint32_t res;
      /* GCC does not optimize the logic below into an unaligned access on
	 targets that support it, so on byte-addressable little-endian targets
	 with 8-bit bytes we use memcpy instead. */
      std::memcpy(&res, get_offset(off), sizeof(res));
      return res;
    }
    if((off & 3) == 0) [[likely]] return Conv(get_exact_ref(off));
    const int bits = (off & 3)*8;
    const uint32_t mask = (uint32_t{1} << bits) - 1;
    const int shift = 32 - bits;
    uint32_t res = 0;
    res = (get_alignedl(off + 4) & mask) << shift;
    res |= (get_alignedl(off) & ~mask) >> bits;
    return res;
  }

  void set_word_raw(uint32_t off, uint32_t word) {
    if constexpr(std::endian::native == std::endian::little &&
		 sizeof(uint32_t) == 4 && CHAR_BIT == 8) {
      //as above
      std::memcpy(get_offset(off), &word, sizeof(word));
    }
    else if((off & 3) == 0) [[likely]] get_exact_ref(off) = Conv(word);
    else {
      const int bits = (off & 3)*8;
      const uint32_t mask = (uint32_t{1} << bits) - 1;
      const int shift = 32 - bits;
      set_alignedl(off + 4, (get_alignedl(off + 4) & ~mask) | word >> shift);
      set_alignedl(off, (get_alignedl(off) & mask) | word << bits);
    }
  }

private:
  uint32_t get_word_impl(uint32_t off) override {
    return get_word_raw(off);
  }

  uint8_t get_byte_impl(uint32_t off) override {
    if(off > get_limit()) return 0;
    return get_alignedl(off) >> (off & 3)*8 & 0xFF;
  }

  void set_word_impl(uint32_t off, uint32_t word) override {
    set_word_raw(off, word);
  }

  void set_byte_impl(uint32_t off, uint8_t byte) override {
    const uint32_t bstart = (off & 3)*8;
    set_alignedl(off, ((get_alignedl(off) & ~(0xFF << bstart))
		       | uint32_t{byte} << bstart));
  }
};

static inline uint32_t uint32_id(uint32_t n) { return n; }

[[gnu::always_inline]]
static inline uint32_t byteconv(uint32_t word) {
  if constexpr(std::endian::native == std::endian::big) {
#ifdef __GNUC__
    word = __builtin_bswap32(word);
#else
    const uint32_t orig = word;
    word = 0;
    word |= orig >> 24;
    word |= orig >> 8 & 0xFF00;
    word |= orig << 8 & 0xFF0000;
    word |= orig << 24 & 0xFF000000;
#endif
  }
  return word;
}

template<bool> class array_device : public fn_array_device<uint32_id> {
public:
  using fn_array_device::fn_array_device;
};

template<> class array_device<false>
  : public fn_array_device<std::endian::native == std::endian::big
			   ? byteconv : uint32_id> {
public:
  using fn_array_device::fn_array_device;
};

class memory;

static memory * largest_memory = nullptr;

class memory final : public array_device<true> {
public:
  memory(uint32_t base, uint32_t lim)
    : array_device{[&]() {
      if(lim >= UINT32_MAX - 3 && UINT32_MAX == SIZE_MAX)
	throw std::bad_alloc();
      const size_t size = (static_cast<size_t>(lim) + 4) >> 2;
      uint32_t * const contents =
	/* On byte-addressable machines, we allocate a character array to
	   allow unaligned access (using memcpy) without undefined behaviour.
	   On other machines, we allocate a uint32_t array to save space. */
	sizeof(uint32_t) == 4
	? reinterpret_cast<uint32_t*>(new char[lim + 1]) : new uint32_t[size];
      std::memset(contents, 0, lim + 1);
      return contents;
    }(), base, lim} {
    if(lim > 0xFFFFFFFB)
      throw std::domain_error{"limit too large"};
    if(!largest_memory || lim > largest_memory->get_limit())
      largest_memory = this;
  }
};

class mmap_device : public array_device<false> {
  mmap_device(uint32_t base, std::pair<uint32_t*, uint32_t> internal)
    : array_device{internal.first, base, internal.second} {}

public:
  mmap_device(const char * name, uint32_t base)
    : mmap_device{base, [&]() {
      uint32_t * contents = NULL;
      int fd;
      if((fd = open(name, O_RDONLY)) == -1) {
	std::cerr << "cannot open " << name << " for reading: ";
	std::perror("");
	std::exit(-3);
      }
      struct stat st;
      if(fstat(fd, &st) == -1) {
	std::cerr << "cannot stat " << name << ": ";
	std::perror("");
	std::exit(-3);
      }
      const uint32_t limit =
	(st.st_size >= UINT32_MAX - 4 ? UINT32_MAX - 4 : st.st_size) - 1;
      const auto ptr = mmap(NULL, limit + 1, PROT_READ, MAP_PRIVATE, fd, 0);
      if((contents = static_cast<uint32_t*>(ptr)) == MAP_FAILED) {
	std::cerr << "cannot map " << name << ": ";
	std::perror("");
	std::exit(-3);
      }
      return std::pair { contents, limit };
    }()} {}
};

class mmap_ROM;

static mmap_ROM * largest_ROM = nullptr;

class mmap_ROM final : public read_only_device<mmap_device> {
public:
  mmap_ROM(const char * name, uint32_t base) : read_only_device{name, base} {
    if(!largest_ROM || get_limit() > largest_ROM->get_limit())
      largest_ROM = this;
  }
};

class stdio : public device {
  std::atomic_bool output_finished;
  std::atomic_bool input_ready;
  uint8_t input;
  uint8_t output;

  void reader() {
    while(true) {
      input = std::cin.get();
      input_ready.wait(input_ready = true);
    }
  }

  void writer() {
    while(true) {
      output_finished.wait(true);
      std::cout.put(output);
      output_finished = true;
    }
  }

public:
  stdio(uint32_t base)
    : device{base, 7}, output_finished{true}, input_ready{false} {
    if(isatty(0)) {
      std::setbuf(stdin, NULL);
      std::setbuf(stdout, NULL);
      struct termios termios;
      tcgetattr(0, &termios);
      termios.c_iflag &= ~(PARMRK | ISTRIP | IXON);
      termios.c_lflag &= ~(ECHO | ICANON | IEXTEN);
      termios.c_cflag &= ~(CSIZE | PARENB);
      termios.c_cflag |= CS8;
      termios.c_cc[VMIN] = 1;
      termios.c_cc[VTIME] = 0;
      tcsetattr(0, TCSANOW, &termios);
    }
    new std::thread(&stdio::reader, this);
    new std::thread(&stdio::writer, this);
  }

private:
  uint8_t iget_byte(uint32_t off, bool input_ready) {
    switch(off) {
    case 0:
      return input_ready ? input : 0;
    case 1:
      return input_ready ? (uint8_t)std::cin.eof() << 1 | 1 : 0;
    case 4:
      return output_finished;
    default:
      return 0;
    }
  }

  uint8_t get_byte_impl(uint32_t off) override {
    return iget_byte(off, input_ready);
  }

  void set_byte_impl(uint32_t off, uint8_t byte) override {
    if(off == 4 && output_finished) {
      output = byte;
      output_finished = false;
      output_finished.notify_one();
    }
  }

  uint32_t get_word_impl(uint32_t off) override {
    uint32_t res = 0;
    bool input_ready = this->input_ready;
    res |= iget_byte(off, input_ready);
    res |= iget_byte(off + 1, input_ready) << 8;
    res |= iget_byte(off + 2, input_ready) << 16;
    res |= iget_byte(off + 3, input_ready) << 24;
    if((off >= (uint32_t)-3 || off == 0) && input_ready) {
      this->input_ready = false;
      this->input_ready.notify_one();
    }
    return res;
  }
};

class ticks : public read_only_device<device> {
public:
  ticks(uint32_t base) : read_only_device{base, 3} {}

private:
  uint32_t get_word_impl(uint32_t off) override {
    if((off & 3) == 0) {
      return static_cast<uint32_t>
	(std::chrono::duration_cast<std::chrono::milliseconds>
	 (std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    const uint32_t bits = (off & 3)*8;
    return get_word_impl(0) << (32 - bits) | get_word_impl(0) >> bits;
  }

  uint8_t get_byte_impl(uint32_t off) override {
    return static_cast<uint8_t>((get_word_impl(0) & (0xFF << off*8)) >> off*8);
  }
};

class zero_device : public read_only_device<device> {
public:
  using read_only_device::read_only_device;

private:
  uint8_t get_byte_impl(uint32_t) override {
    return 0;
  }
};

static device * get_device(uint32_t addr) {
  return std::visit([&](const auto& val3) {
    if constexpr(std::is_same_v<decltype(val3), device* const&>)
      return val3;
    else return std::visit([&](const auto& val2) {
      if constexpr(std::is_same_v<decltype(val2), device* const&>)
	return val2;
      else return (*val2)[(addr >> 2) & 0x3FF];
    }, (*val3)[(addr >> 12) & 0x3FF]);
  }, devtab[addr >> 22]);
}

static inline uint8_t get_byte(uint32_t addr) {
  device * const dev = get_device(addr);
  return dev->get_byte(addr - dev->get_base());
}

[[maybe_unused]]
static inline void set_byte(uint32_t addr, uint8_t byte) {
  device * const dev = get_device(addr);
  dev->set_byte(addr - dev->get_base(), byte);
}

static inline uint32_t get_word(uint32_t addr) {
  device * const dev1 = get_device(addr);
  uint32_t res = dev1->get_word(addr - dev1->get_base());
  if(addr & 3) {
    device * const dev2 = get_device(addr + 3);
    if(dev1 != dev2)
      res |= dev2->get_word(addr - dev2->get_base());
  }
  return res;
}

static inline void set_word(uint32_t addr, uint32_t word) {
  device * const dev1 = get_device(addr);
  dev1->set_word(addr - dev1->get_base(), word);
  if(addr & 3) {
    device * const dev2 = get_device(addr);
    if(dev1 != dev2)
      dev2->set_word(addr - dev2->get_base(), word);
  }
}

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

class CPU {
  struct breakpoint {
    int num;
    uint32_t addr;
  };

  uint32_t regs[8] = { 0 };
  uint32_t pc = 0;
  bool Z = false, N = false, cmp = false;
  std::vector<breakpoint> breakpoints;
  int next_breakpoint = 1;

public:
  void add_breakpoint(uint32_t addr) {
    breakpoints.push_back({ next_breakpoint++, addr });
  }

  void execute() {
    device * const largest_readable =
      largest_ROM && (!largest_memory ||
		      largest_ROM->get_limit() > largest_memory->get_limit())
      ? static_cast<device*>(largest_ROM)
      : static_cast<device*>(largest_memory);

    bool single_step = false;
    for(;; pc += 4) {
      const auto get = [&](uint32_t addr) {
	if(word_in_device_range(addr, largest_readable)) {
	  if(largest_readable == largest_ROM)
	    return
	      largest_ROM->get_word_raw(addr - largest_readable->get_base());
	  else
	    return
	      largest_memory->get_word_raw(addr - largest_readable->get_base());
	}
	else return get_word(addr);
      };

      const uint32_t inst = get(pc);

      for(auto it = breakpoints.cbegin(); it != breakpoints.cend();) {
	if(pc == it->addr) {
	  single_step = true;
	  if(it->num == -1) {
	    it = breakpoints.erase(it);
	    continue;
	  }
	  std::cerr << "breakpoint " << it->num
		    << " at 0x" << std::hex << it->addr << std::dec << '\n';
	}
	++it;
      }

      if(single_step) {
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
};

int main(int argc, char * const * argv) {
  new zero_device{0, 0xFFFFFFFF};
  std::optional<uint32_t> stdio_base;
  std::optional<uint32_t> ticks_base;
  const option opts[] = {
    { .name = "stdio", .has_arg = true, .flag = NULL, .val = 's' },
    { .name = "memory", .has_arg = true, .flag = NULL, .val = 'm' },
    { .name = "rom", .has_arg = true, .flag = NULL, .val = 'r' },
    { .name = "break", .has_arg = true, .flag = NULL, .val = 'b' },
    { .name = "ticks", .has_arg = true, .flag = NULL, .val = 't' },
    { .name = NULL, .has_arg = false, .flag = NULL, .val = 0 }
  };
  int c;
  int longindex;
  CPU cpu;
  const auto bad_number = [&]() {
    std::cerr << "bad number supplied to option --" << opts[longindex].name
	      << '\n';
    std::exit(-1);
  };
  const auto no_comma = [&]() {
    std::cerr << "no comma in argument supplied to option --"
	      << opts[longindex].name << '\n';
    std::exit(-1);
  };
  const auto parse_number = [&](const char * start, const char * end) {
    if(end - start >= 2 && start[0] == '0' && start[1] == 'x')
      start += 2;
    uint32_t res;
    if(std::from_chars(start, end, res, 16).ec != std::errc{})
      bad_number();
    return res;
  };
  const auto parse_comma = [&]() {
    const char * const comma = std::strchr(optarg, ',');
    if(!comma) no_comma();
    const uint32_t value = parse_number(optarg, comma);
    return std::pair{value, comma + 1};
  };
  while((c = getopt_long(argc, argv, "s:m:r:b:", opts, &longindex)) != -1) {
    switch(c) {
    case 's':
      stdio_base = parse_number(optarg, optarg + strlen(optarg));
      break;
    case 'm':
      { const auto [base, end] = parse_comma();
	const uint32_t lim = parse_number(end, end + strlen(end));
	new memory(base, lim);
      }
      break;
    case 'r':
      { const auto [value, path] = parse_comma();
	new mmap_ROM(path, value);
      }
      break;
    case 't':
      ticks_base = parse_number(optarg, optarg + strlen(optarg));
      break;
    case 'b':
      cpu.add_breakpoint(parse_number(optarg, optarg + strlen(optarg)));
      break;
    case '?':
      return -1;
    default:
      assert(false);
      return -1;
    }
  }
  if(stdio_base) new stdio(*stdio_base);
  if(ticks_base) new ticks(*ticks_base);
  cpu.execute();
}
