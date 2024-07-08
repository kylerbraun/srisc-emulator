// -*- C++ -*-
#ifndef DEVICE_H_
#define DEVICE_H_
#include <memory>
#include <variant>
#include <array>
#include <atomic>
#include <cstring>
#include <cstdint>

// Represents a memory-mapped device.
class device {
  std::uint32_t base;
  std::uint32_t lim;

public:
  /* Initializes the base address and limit of the device to base and lim
     respectively. */
  device(std::uint32_t base, std::uint32_t lim);
  device(const device&) = delete;
  device(device&&) = delete;

protected:
  ~device() {}

public:
  device& operator=(const device&) = delete;
  device& operator=(device&&) = delete;

  // Returns the first address managed by the device.
  std::uint32_t get_base() { return base; }
  /* Returns the offset from the first address of last address managed by the
     device. */
  std::uint32_t get_limit() { return lim; }

private:
  /* Called by get_byte to read a single byte at the given offset within the
     region managed by this device.  The offset is guaranteed not to exceed the
     limit. */
  virtual std::uint8_t get_byte_impl(std::uint32_t) = 0;
  /* Called by set_byte to write the given byte at the given offset within
     the region managed by this device to the given value.  The offset is
     guaranteed not to exceed the limit. */
  virtual void set_byte_impl(std::uint32_t, std::uint8_t) = 0;

public:
  /* If the given offset is within the region managed by this device, this
     function reads the value at that offset.  Otherwise, 0 is returned. */
  std::uint8_t get_byte(std::uint32_t off) {
    if(off > lim) return 0;
    return get_byte_impl(off);
  }

  /* If the given offset is within the region managed by this device, this
     function writes the given value at that offset. */
  void set_byte(std::uint32_t off, std::uint8_t byte) {
    if(off > lim) return;
    set_byte_impl(off, byte);
  }

private:
  /* Called by get_word to read a word at the given offset.  The given offset
     may exceed the limit.  Accesses should wrap around on overflow. */
  virtual std::uint32_t get_word_impl(std::uint32_t off) {
    std::uint32_t res = 0;
    res |= get_byte(off);
    res |= get_byte(off + 1) << 8;
    res |= get_byte(off + 2) << 16;
    res |= get_byte(off + 3) << 24;
    return res;
  }

  /* Called by set_word to write the given word at the given offset.  The given
     offset may exceed the limit.  Accesses should wrap around on overflow. */
  virtual void set_word_impl(std::uint32_t off, std::uint32_t word) {
    set_byte(off, word & 0xFF);
    set_byte(off + 1, word >> 8 & 0xFF);
    set_byte(off + 2, word >> 16 & 0xFF);
    set_byte(off + 3, word >> 24 & 0xFF);
  }

  /* This function masks out bytes in word that would fall outside the region
     managed by this device if word were to be read or written at the given
     offset. */
  std::uint32_t clean_word(std::uint32_t off, std::uint32_t word) {
    if(off > lim && off <= (std::uint32_t)-4) return 0;
    if(lim - off < 3) word &= (std::uint32_t)0xFFFFFFFF >> (3 - (lim - off))*8;
    if(off > (std::uint32_t)-4) word &= (std::uint32_t)0xFFFFFFFF << (-off)*8;
    return word;
  }

public:
  /* This function reads a word at the given offset within the region managed
     by this device.  Any bytes that fall outside this region will be read as
     zero.  If an overflow occurs, the access will wrap around. */
  std::uint32_t get_word(std::uint32_t off) {
    return clean_word(off, get_word_impl(off));
  }

  /* This function writes the given word at the given offset within the region
     managed by this device.  Zero will be written in place of any bytes that
     fall outside this region.  If an overflow occurs, the access will wrap
     around. */
  void set_word(std::uint32_t off, std::uint32_t word) {
    set_word_impl(off, clean_word(off, word));
  }
};

/* When a read or write occurs at some address, some efficient method is needed
   to determine which device should manage the access.  This program uses a
   page-table-like tree of pointers to accomplish this.  (Note that accesses to
   the largest memory or ROM device bypass the device system entirely for
   efficiency.)  Each table has 1024 entries.  The lowest level table contains
   pointers to the devices.  Each entry in the lowest level table represents
   4 bytes.  Each entry in a higher level table contains either a pointer to
   a device, analogous to large pages, or a pointer to a lower level table. */

/* If Entry is the type of a level n table, NLE<Entry> is the type of a level
   n + 1 table. */
template<typename Entry> using NLE =
  std::variant<device*, std::unique_ptr<std::array<Entry, 1024>>>;
using L2E = NLE<device*>;
using L3E = NLE<L2E>;

/* If T is the type of some level of table, this structure will contain a signle
   static member, value, indicating the length of right shift necessary to
   obtain an index into the table.  It is not necessary to indicate a mask, as
   the indices are always 10 bits long. */
template<typename T> struct shift;

template<> struct shift<std::array<device*, 1024>> {
  static constexpr int value = 2;
};

template<typename Entry> struct shift<std::array<NLE<Entry>, 1024>> {
  static constexpr int value = shift<std::array<Entry, 1024>>::value + 10;
};

extern std::array<L3E, 1024> devtab;

/* This function checks whether all the bytes in the word starting at addr are
   within the range defined by base and limit.  Note that the region cannot wrap
   around the address space. */
inline bool word_in_range(std::uint32_t addr,
			  std::uint32_t base, std::uint32_t limit) {
  return addr >= base && addr + 3 - base <= limit;
}

/* This function converts the given word between native byte order and little
   endian byte order (this function is an involution).  This function is not
   used, as one might suspect, to ensure that word-size accesses are consistent
   with byte-size accesses (byte size accesses are performed by first making a
   word size access and then extracting the relevant byte; this should in
   principle allow the emulator to run on platforms with multibyte chars).
   Instead, this function is used to ensure that backing files for ROM and
   memory devices are portable between architectures of different endianness.
   Note that this scheme does not support middle-endian architectures, or
   platforms where CHAR_BIT is not 8, so the portability guarantees mentioned
   above do not apply in those situations. */
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

/* This function returns a reference to the word-aligned word in contents that
   includes the byte at offset off. */
inline std::uint32_t& get_aligned_ref(std::uint32_t * contents,
				      std::uint32_t off) {
  return contents[off >> 2];
}

/* This function returns a character pointer at offset off from contents. */
inline char * get_offset(std::uint32_t * contents, std::uint32_t off) {
  return reinterpret_cast<char*>(contents) + off;
}

/* This function is an optimization of get_aligned ref for the special case when
   off is a multiple of 4.  GCC is unable to optimize out the bitshift in
   get_aligned_ref even if we first make sure the offset is aligned.  We
   therefore need this function to optimize out the bitshift. */
inline std::uint32_t& get_exact_ref(std::uint32_t * contents,
				    std::uint32_t off) {
  if constexpr(sizeof(std::uint32_t) == 4)
    return *reinterpret_cast<std::uint32_t*>(get_offset(contents, off));
  else return get_aligned_ref(contents, off);
}

/* If get_aligned_ref(contents, off) refers to a word whose index is not greater
   than limit, this function returns the value of byteconv applied to that word.
   Otherwise, this function returns 0. */
inline std::uint32_t get_alignedl(std::uint32_t * contents, std::uint32_t limit,
			   std::uint32_t off) {
  if(off >> 2 <= limit >> 2)
    return byteconv(get_aligned_ref(contents, off));
  else return 0;
}

/* If get_aligned_ref(contents, off) refers to a word whose offset is not
   greater than limit, this function assigns to that word the value of byteconv
   applied to word. */
inline void set_alignedl(std::uint32_t * contents, std::uint32_t limit,
			 std::uint32_t off, std::uint32_t word) {
  if(off >> 2 <= limit >> 2)
    get_aligned_ref(contents, off) = byteconv(word);
}

/* This function returns the word that would result from a possibly unaligned
   access at off bytes from contents on a little-endian machine.  This is
   accomplished with aligned accesses and bitwise operations.  Aligned words at
   an offset greater than limit are read as zero. */
inline std::uint32_t get_word_raw(std::uint32_t * contents, std::uint32_t limit,
				  std::uint32_t off) {
  if constexpr(sizeof(std::uint32_t) == 4 && CHAR_BIT == 8) {
    std::uint32_t res;
    /* GCC does not optimize the bitshift logic below into an unaligned access
       on targets that support it, so on byte-addressable targets with 8-bit
       bytes we use memcpy instead. */
    std::memcpy(&res, get_offset(contents, off), sizeof(res));
    return byteconv(res);
  }
  if((off & 3) == 0) [[likely]] return byteconv(get_exact_ref(contents, off));
  const int bits = (off & 3)*8;
  const std::uint32_t mask = (std::uint32_t{1} << bits) - 1;
  const int shift = 32 - bits;
  std::uint32_t res = 0;
  res = (get_alignedl(contents, limit, off + 4) & mask) << shift;
  res |= (get_alignedl(contents, limit, off) & ~mask) >> bits;
  return res;
}

/* This function has the same effect as writing word at the possibly-unaligned
   offset off from contents.  This is accomplished with aligned writes and
   bitwise operations.  Aligned words at an offset greater than limit are not
   written. */
inline void set_word_raw(std::uint32_t * contents, std::uint32_t limit,
			 std::uint32_t off, std::uint32_t word) {
  if constexpr(sizeof(std::uint32_t) == 4 && CHAR_BIT == 8) {
    const std::uint32_t src = byteconv(word);
    //as above
    std::memcpy(get_offset(contents, off), &src, sizeof(src));
  }
  else if((off & 3) == 0)
    [[likely]] get_exact_ref(contents, off) = byteconv(word);
  else {
    const int bits = (off & 3)*8;
    const std::uint32_t mask = (std::uint32_t{1} << bits) - 1;
    const int shift = 32 - bits;
    set_alignedl(contents, limit, off + 4,
		 (get_alignedl(contents, limit, off + 4) & ~mask)
		 | word >> shift);
    set_alignedl(contents, limit, off,
		 (get_alignedl(contents, limit, off) & mask) | word << bits);
  }
}

/* This device represents an address region backed by a uint32_t array. */
class array_device : public device {
  std::uint32_t * const contents;

protected:
  /* Constructs an array device backed by contents with base base and limit
     lim. */
  array_device(std::uint32_t * contents, std::uint32_t base, std::uint32_t lim);

public:
  std::uint32_t * get_contents() {
    return contents;
  }

  /* This function shadows a ROM described by fd.  off is the offset from
     contents at which to shadow. */
  void shadow_ROM(std::uint32_t off, int fd, std::uint32_t lim);

private:
  std::uint32_t get_word_impl(std::uint32_t off) override {
    return get_word_raw(contents, get_limit(), off);
  }

  std::uint8_t get_byte_impl(std::uint32_t off) override {
    if(off > get_limit()) return 0;
    return get_alignedl(contents, get_limit(), off) >> (off & 3)*8 & 0xFF;
  }

  void set_word_impl(std::uint32_t off, std::uint32_t word) override {
    set_word_raw(contents, get_limit(), off, word);
  }

  void set_byte_impl(std::uint32_t off, std::uint8_t byte) override {
    const std::uint32_t bstart = (off & 3)*8;
    set_alignedl(contents, get_limit(), off,
		 ((get_alignedl(contents, get_limit(), off) & ~(0xFF << bstart))
		  | std::uint32_t{byte} << bstart));
  }
};

/* This variable points to the largest array device in the system. */
extern array_device * largest_readable;

/* This device represents an address region backed by allocated memory. */ 
class memory final : public array_device {
public:
  memory(std::uint32_t base, std::uint32_t lim);
};

/* This variable points to the largest memory device in the system. */
extern memory * largest_memory;

/* This device represents an address region backed by a memory-mapped file. */
class mmap_device : public array_device {
public:
  mmap_device(int fd, std::uint32_t base, std::uint32_t lim);
};

/* If T is a device, read_only_device<T> is a read-only version of that device.
   Writes to a read-only device are ignored. */
template<typename T> class read_only_device : public T {
protected:
  using T::T;

private:
  void set_byte_impl(std::uint32_t, std::uint8_t) override {}
  void set_word_impl(std::uint32_t, std::uint32_t) override {}
};

/* This device represents a memory-mapped ROM. */
class mmap_ROM final : public read_only_device<mmap_device> {
public:
  mmap_ROM(int, std::uint32_t, std::uint32_t);
};

/* This device allows access to the emulator's stdin and stdout streams.  Its
   address region is always 8 bytes in size.  Bytes read/written to/from
   stdin/stdout are read/written asynchronously.  Note that one byte will be
   read from stdin on startup, even if no reads of this device occur.  The
   addresses are interpreted as follows:
   base + 0: If an input byte is available from stdin, a read from this address
     will read that byte.  Otherwise, 0 is read.
   base + 1: Bit 0 is read as 1 if an input byte is available from stdin, and 0
     otherwise.  If an input byte is available from stdin, bit 1 is 1 if eof has
     been reached on stdin, and 0 otherwise.
   base + 4: Bit 0 is read as 1 if the last byte written to stdout has finished
     being written, and 0 otherwise.  If no byte is in the process of being
     written to stdout, a byte written to this address will be written to
     stdout.
   If the value of a bit on reading is not specified in the above, the value of
   that bit when read is unspecified.  If the effect of writing a particular bit
   is not specified in the above, then writes of that bit must satisfy the
   following conditions:
   (a) The bit must be written with the value that was most recently read from
       it,
   (b) The most recent read containing the bit must have been of the same size
       and location of the write containing the bit, and
   (c) There must not have been any other reads/writes to/from this device
       between the most recent read of the bit and this write.
   */
class stdio : public device {
  std::atomic_bool output_finished;
  std::atomic_bool input_ready;
  std::uint8_t input;
  std::uint8_t output;

  // Contains reader thread loop.
  void reader();
  // Contains writer thread loop.
  void writer();

public:
  stdio(std::uint32_t base);

private:
  /* This is a version of get_byte_impl that accepts an input_ready parameter
     instead of using the member variable.  This allows get_word_impl to provide
     a consistent view of the device in spite of making multiple calls. */
  std::uint8_t iget_byte(std::uint32_t addr, bool input_ready);
  std::uint8_t get_byte_impl(std::uint32_t) override;
  void set_byte_impl(std::uint32_t, std::uint8_t) override;
  std::uint32_t get_word_impl(std::uint32_t) override;
};

/* This device allows limited access to the host system's clock.  It is meant to
   be useful for measuring duration, but not for telling time. */
class ticks : public read_only_device<device> {
public:
  ticks(std::uint32_t base);

private:
  std::uint32_t get_word_impl(std::uint32_t) override;
  std::uint8_t get_byte_impl(std::uint32_t) override;
};

/* This device always reads as zero and ignores writes.  It is used for regions
   of memory where no other device has been mapped. */
class zero_device : public read_only_device<device> {
public:
  using read_only_device::read_only_device;

private:
  std::uint8_t get_byte_impl(std::uint32_t) override;
};

// Returns a pointer to the device mapped at the given address.
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

// Reads a byte from the given address.
inline std::uint8_t get_byte(std::uint32_t addr) {
  device * const dev = get_device(addr);
  return dev->get_byte(addr - dev->get_base());
}

// Writes a byte to the given address.
inline void set_byte(std::uint32_t addr, std::uint8_t byte) {
  device * const dev = get_device(addr);
  dev->set_byte(addr - dev->get_base(), byte);
}

// Reads a word from the given address.
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

// Writes a word to the given address.
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
