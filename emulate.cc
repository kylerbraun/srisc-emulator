#define _POSIX_C_SOURCE 200809L
#include "cpu.h"
#include "device.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <iostream>
#include <vector>
#include <utility>
#include <optional>
#include <charconv>
#include <system_error>
#include <type_traits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

using std::uint32_t;

// Casts val to an unsigned integer of the same size as T.
template<typename T> static auto make_unsigned(T val) {
  return static_cast<std::make_unsigned_t<T>>(val);
}

/* Opens the file named by the given filename for reading, returning the file
   descriptor and the limit. */
static auto open_ROM(const char * name) {
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
    (make_unsigned(st.st_size) >= UINT32_MAX - 4
     ? UINT32_MAX - 4 : st.st_size) - 1;
  return std::pair{fd, limit};
}

int main(int argc, char * const * argv) {
  /* Create a device covering all of memory.  Other devices will later override
     this within specific subranges, but this will still catch accesses not
     covered by any other device. */
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
  const auto parse_number1 = [&](const char * start) {
    return parse_number(start, start + strlen(start));
  };
  const auto parse_comma = [&]() {
    const char * const comma = std::strchr(optarg, ',');
    if(!comma) no_comma();
    const uint32_t value = parse_number(optarg, comma);
    return std::pair{value, comma + 1};
  };
  std::vector<std::pair<uint32_t, const char*>> memories, ROMs;
  while((c = getopt_long(argc, argv, "s:m:r:b:t:", opts, &longindex)) != -1) {
    switch(c) {
    case 's':
      stdio_base = parse_number1(optarg);
      break;
    case 'm':
      memories.push_back(parse_comma());
      break;
    case 'r':
      ROMs.push_back(parse_comma());
      break;
    case 't':
      ticks_base = parse_number1(optarg);
      break;
    case 'b':
      cpu.add_breakpoint(parse_number1(optarg));
      break;
    case '?':
      return -1;
    default:
      assert(false);
      return -1;
    }
  }
  for(const auto& args : memories)
    new memory(args.first, parse_number1(args.second));
  for(const auto& args : ROMs) {
    const auto [fd, limit] = open_ROM(args.second);
    device * const start = get_device(args.first);
    device * const end = get_device(args.first + limit);
    /* If the ROM is to be loaded into a region backed by a single memory
       device, shadow it instead of creating a new ROM device. */
    if(start == end && typeid(*start) == typeid(memory)) {
      const auto mem = static_cast<memory*>(start);
      mem->shadow_ROM(args.first - mem->get_base(), fd, limit);
      close(fd);
    }
    else new mmap_ROM(fd, args.first, limit);
  }
  if(stdio_base) new stdio(*stdio_base);
  if(ticks_base) new ticks(*ticks_base);
  cpu.execute();
}
