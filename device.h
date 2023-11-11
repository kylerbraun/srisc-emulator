// -*- C++ -*-
#ifndef DEVICE_H_
#define DEVICE_H_
#include <memory>
#include <variant>
#include <array>
#include <atomic>
#include <cstring>
#include <cstdint>

class device {
  std::uint32_t base;
  std::uint32_t lim;

public:
  device(std::uint32_t base, std::uint32_t lim);
  device(const device&) = delete;
  device(device&&) = delete;

protected:
  ~device() {}

public:
  device& operator=(const device&) = delete;
  device& operator=(device&&) = delete;

  std::uint32_t get_base() { return base; }
  std::uint32_t get_limit() { return lim; }

private:
  virtual std::uint8_t get_byte_impl(std::uint32_t) = 0;
  virtual void set_byte_impl(std::uint32_t, std::uint8_t) = 0;

public:
  std::uint8_t get_byte(std::uint32_t off) {
    if(off > lim) return 0;
    return get_byte_impl(off);
  }

  void set_byte(std::uint32_t off, std::uint8_t byte) {
    if(off > lim) return;
    set_byte_impl(off, byte);
  }

private:
  virtual std::uint32_t get_word_impl(std::uint32_t off) {
    std::uint32_t res = 0;
    res |= get_byte(off);
    res |= get_byte(off + 1) << 8;
    res |= get_byte(off + 2) << 16;
    res |= get_byte(off + 3) << 24;
    return res;
  }

  virtual void set_word_impl(std::uint32_t off, std::uint32_t word) {
    set_byte(off, word & 0xFF);
    set_byte(off + 1, word >> 8 & 0xFF);
    set_byte(off + 2, word >> 16 & 0xFF);
    set_byte(off + 3, word >> 24 & 0xFF);
  }

  std::uint32_t clean_word(std::uint32_t off, std::uint32_t word) {
    if(off > lim && off <= (std::uint32_t)-4) return 0;
    if(lim - off < 3) word &= (std::uint32_t)0xFFFFFFFF >> (3 - (lim - off))*8;
    if(off > (std::uint32_t)-4) word &= (std::uint32_t)0xFFFFFFFF << (-off)*8;
    return word;
  }

public:
  std::uint32_t get_word(std::uint32_t off) {
    return clean_word(off, get_word_impl(off));
  }

  void set_word(std::uint32_t off, std::uint32_t word) {
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

extern std::array<L3E, 1024> devtab;

inline bool byte_in_device_range(std::uint32_t addr, device * dev) {
  return addr >= dev->get_base() && addr - dev->get_base() < dev->get_limit();
}

inline bool word_in_device_range(std::uint32_t addr, device * dev) {
  return dev && addr >= dev->get_base()
    && addr + 3 - dev->get_base() < dev->get_limit();
}
[[gnu::always_inline]]
inline std::uint32_t byteconv(std::uint32_t word) {
  if constexpr(std::endian::native == std::endian::big) {
#ifdef __GNUC__
    word = __builtin_bswap32(word);
#else
    const std::uint32_t orig = word;
    word = 0;
    word |= orig >> 24;
    word |= orig >> 8 & 0xFF00;
    word |= orig << 8 & 0xFF0000;
    word |= orig << 24 & 0xFF000000;
#endif
  }
  return word;
}

class array_device : public device {
  std::uint32_t * const contents;

protected:
  array_device(std::uint32_t*, std::uint32_t, std::uint32_t);

private:
  std::uint32_t& get_aligned_ref(std::uint32_t off) {
    return contents[off >> 2];
  }

  char * get_offset(std::uint32_t off) {
    return reinterpret_cast<char*>(contents) + off;
  }

  /* GCC is unable to optimize out the bitshift in get_aligned_ref even if we
     first make sure the offset is aligned.  We therefore need this function to
     optimize out the bitshift. */
  std::uint32_t& get_exact_ref(std::uint32_t off) {
    if constexpr(sizeof(std::uint32_t) == 4)
      return *reinterpret_cast<std::uint32_t*>(get_offset(off));
    else return get_aligned_ref(off);
  }

  std::uint32_t get_alignedl(std::uint32_t off) {
    if(off >> 2 <= get_limit() >> 2)
      return byteconv(get_aligned_ref(off));
    else return 0;
  }

  void set_alignedl(std::uint32_t off, std::uint32_t word) {
    if(off >> 2 <= get_limit() >> 2)
      get_aligned_ref(off) = byteconv(word);
  }

public:
  std::uint32_t get_word_raw(std::uint32_t off) {
    if constexpr(sizeof(std::uint32_t) == 4 && CHAR_BIT == 8) {
      std::uint32_t res;
      /* GCC does not optimize the bitshift logic below into an unaligned access
	 on targets that support it, so on byte-addressable targets with 8-bit
	 bytes we use memcpy instead. */
      std::memcpy(&res, get_offset(off), sizeof(res));
      return byteconv(res);
    }
    if((off & 3) == 0) [[likely]] return byteconv(get_exact_ref(off));
    const int bits = (off & 3)*8;
    const std::uint32_t mask = (std::uint32_t{1} << bits) - 1;
    const int shift = 32 - bits;
    std::uint32_t res = 0;
    res = (get_alignedl(off + 4) & mask) << shift;
    res |= (get_alignedl(off) & ~mask) >> bits;
    return res;
  }

  void set_word_raw(std::uint32_t off, std::uint32_t word) {
    if constexpr(sizeof(std::uint32_t) == 4 && CHAR_BIT == 8) {
      const std::uint32_t src = byteconv(word);
      //as above
      std::memcpy(get_offset(off), &src, sizeof(src));
    }
    else if((off & 3) == 0) [[likely]] get_exact_ref(off) = byteconv(word);
    else {
      const int bits = (off & 3)*8;
      const std::uint32_t mask = (std::uint32_t{1} << bits) - 1;
      const int shift = 32 - bits;
      set_alignedl(off + 4, (get_alignedl(off + 4) & ~mask) | word >> shift);
      set_alignedl(off, (get_alignedl(off) & mask) | word << bits);
    }
  }

  void shadow_ROM(std::uint32_t, int, std::uint32_t);

private:
  std::uint32_t get_word_impl(std::uint32_t off) override {
    return get_word_raw(off);
  }

  std::uint8_t get_byte_impl(std::uint32_t off) override {
    if(off > get_limit()) return 0;
    return get_alignedl(off) >> (off & 3)*8 & 0xFF;
  }

  void set_word_impl(std::uint32_t off, std::uint32_t word) override {
    set_word_raw(off, word);
  }

  void set_byte_impl(std::uint32_t off, std::uint8_t byte) override {
    const std::uint32_t bstart = (off & 3)*8;
    set_alignedl(off, ((get_alignedl(off) & ~(0xFF << bstart))
		       | std::uint32_t{byte} << bstart));
  }
};

extern array_device * largest_readable;

class memory final : public array_device {
public:
  memory(std::uint32_t, std::uint32_t);
};

extern memory * largest_memory;

class mmap_device : public array_device {
public:
  mmap_device(int, std::uint32_t, std::uint32_t);
};

template<typename T> class read_only_device : public T {
protected:
  using T::T;

private:
  void set_byte_impl(std::uint32_t, std::uint8_t) override {}
  void set_word_impl(std::uint32_t, std::uint32_t) override {}
};

class mmap_ROM final : public read_only_device<mmap_device> {
public:
  mmap_ROM(int, std::uint32_t, std::uint32_t);
};

class stdio : public device {
  std::atomic_bool output_finished;
  std::atomic_bool input_ready;
  std::uint8_t input;
  std::uint8_t output;

  void reader();
  void writer();

public:
  stdio(std::uint32_t);

private:
  std::uint8_t iget_byte(std::uint32_t, bool);
  std::uint8_t get_byte_impl(std::uint32_t) override;
  void set_byte_impl(std::uint32_t, std::uint8_t) override;
  std::uint32_t get_word_impl(std::uint32_t) override;
};

class ticks : public read_only_device<device> {
public:
  ticks(std::uint32_t);

private:
  std::uint32_t get_word_impl(std::uint32_t) override;
  std::uint8_t get_byte_impl(std::uint32_t) override;
};

class zero_device : public read_only_device<device> {
public:
  using read_only_device::read_only_device;

private:
  std::uint8_t get_byte_impl(std::uint32_t) override;
};

inline device * get_device(std::uint32_t addr) {
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

inline std::uint8_t get_byte(std::uint32_t addr) {
  device * const dev = get_device(addr);
  return dev->get_byte(addr - dev->get_base());
}

inline void set_byte(std::uint32_t addr, std::uint8_t byte) {
  device * const dev = get_device(addr);
  dev->set_byte(addr - dev->get_base(), byte);
}

inline std::uint32_t get_word(std::uint32_t addr) {
  device * const dev1 = get_device(addr);
  std::uint32_t res = dev1->get_word(addr - dev1->get_base());
  if(addr & 3) {
    device * const dev2 = get_device(addr + 3);
    if(dev1 != dev2)
      res |= dev2->get_word(addr - dev2->get_base());
  }
  return res;
}

inline void set_word(std::uint32_t addr, std::uint32_t word) {
  device * const dev1 = get_device(addr);
  dev1->set_word(addr - dev1->get_base(), word);
  if(addr & 3) {
    device * const dev2 = get_device(addr);
    if(dev1 != dev2)
      dev2->set_word(addr - dev2->get_base(), word);
  }
}

#endif
