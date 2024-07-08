#define _POSIX_C_SOURCE 200809L
#include "device.h"
#include <termios.h>
#include <unistd.h>
#include <sys/mman.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <utility>
#include <cstdio>
#include <cassert>

using std::uint32_t;
using std::uint8_t;

std::array<L3E, 1024> devtab;

template<typename Entry>
static void set_devent(NLE<Entry>&, uint32_t, uint32_t, device*);

/* set_devtab maps a device to the region designated by the given base and limit
   within the given device table.  The device table can be at any level.  base
   is relative to the base of the table.  The designated region must fall
   entirely within the range of the table. */

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
    /* Use large "pages" to cover as much of the designated region as possible.
     */
    for(long long i = os != 0; i < (long long)li - si + (ol == mask); i++)
      tab[si + i] = dev;
  // Handle leftover regions not covered by the large "pages."
  if(os != 0) set_devent(tab[si], os, zero ? ol : mask, dev);
  if(ol != mask && (os == 0 || !zero)) set_devent(tab[li], 0, ol, dev);
}

/* set_devent works like set_devtab, except that it operates on a table
   entry rather than an entire table. */

template<typename Entry> static void set_devent(NLE<Entry>& ent, uint32_t base,
						uint32_t lim, device * dev) {
  const auto ptr = std::visit([&](const auto& val) {
    if constexpr(std::is_same_v<decltype(val), device* const&>) {
      /* If this is a large "page," split it into smaller "pages" before doing
	 anything else. */
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

array_device * largest_readable = nullptr;

array_device::array_device(uint32_t * contents, uint32_t base, uint32_t lim)
  : device{base, lim}, contents{contents} {
  if(!largest_readable || lim > largest_readable->get_limit())
    largest_readable = this;
}

void array_device::shadow_ROM(uint32_t off, int fd, uint32_t lim) {
  assert(std::uint64_t{off} + lim <= get_limit());
  char * cur = get_offset(contents, off);
  std::size_t left = std::size_t{lim} + 1;
  ssize_t nread;
  while((nread = read(fd, cur, left)) > 0) {
    cur += nread;
    left -= nread;
  }
  if(nread == -1) {
    std::perror("cannot read ROM");
    std::exit(-3);
  }
}

memory::memory(uint32_t base, uint32_t lim)
  : array_device{[&]() {
    if(lim >= UINT32_MAX - 3 && UINT32_MAX == SIZE_MAX)
      throw std::bad_alloc();
    const long pagesize = sysconf(_SC_PAGESIZE);
    const std::align_val_t align = static_cast<std::align_val_t>(pagesize);
    const uint32_t ps = static_cast<uint32_t>(pagesize);
    lim = ((lim + ps) & ~ps) - 1;
    const size_t size = (static_cast<size_t>(lim) + 4) >> 2;
    uint32_t * const contents =
      /* On byte-addressable machines, we allocate a character array to
	 allow unaligned access (using memcpy) without undefined behaviour.
	 On other machines, we allocate a uint32_t array to save space. */
      sizeof(uint32_t) == 4
      ? reinterpret_cast<uint32_t*>(new(align) char[lim + 1])
      : new(align) uint32_t[size];
    std::memset(contents, 0, lim + 1);
    return contents;
  }(), base, lim} {
  if(lim > 0xFFFFFFFB)
    throw std::domain_error{"limit too large"};
  if(!largest_memory || lim > largest_memory->get_limit())
    largest_memory = this;
}

memory * largest_memory = nullptr;

mmap_device::mmap_device(int fd, uint32_t base, uint32_t limit)
  : array_device{[&]() {
    uint32_t * contents = NULL;
    const auto ptr = mmap(NULL, limit + 1, PROT_READ, MAP_PRIVATE, fd, 0);
    if((contents = static_cast<uint32_t*>(ptr)) == MAP_FAILED) {
      std::perror("cannot map ROM");
      std::exit(-3);
    }
    return contents;
  }(), base, limit} {}

mmap_ROM::mmap_ROM(int fd, uint32_t base, uint32_t limit)
  : read_only_device{fd, base, limit} {}

void stdio::reader() {
  while(true) {
    input = std::cin.get();
    input_ready.wait(input_ready = true);
  }
}

void stdio::writer() {
  while(true) {
    output_finished.wait(true);
    std::cout.put(output);
    output_finished = true;
  }
}

stdio::stdio(uint32_t base)
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

uint8_t stdio::iget_byte(uint32_t off, bool input_ready) {
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

uint8_t stdio::get_byte_impl(uint32_t off) {
  return iget_byte(off, input_ready);
}

void stdio::set_byte_impl(uint32_t off, uint8_t byte) {
  if(off == 4 && output_finished) {
    output = byte;
    output_finished = false;
    output_finished.notify_one();
  }
}

uint32_t stdio::get_word_impl(uint32_t off) {
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

ticks::ticks(uint32_t base) : read_only_device{base, 3} {}

uint32_t ticks::get_word_impl(uint32_t off) {
  if((off & 3) == 0) {
    return static_cast<uint32_t>
      (std::chrono::duration_cast<std::chrono::milliseconds>
       (std::chrono::steady_clock::now().time_since_epoch()).count());
  }
  const uint32_t bits = (off & 3)*8;
  return get_word_impl(0) << (32 - bits) | get_word_impl(0) >> bits;
}

uint8_t ticks::get_byte_impl(uint32_t off) {
  return static_cast<uint8_t>((get_word_impl(0) & (0xFF << off*8)) >> off*8);
}

uint8_t zero_device::get_byte_impl(uint32_t) {
  return 0;
}
