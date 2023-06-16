#pragma once
#include <atomic>
#include <bits/types/struct_timespec.h>
#include <bits/types/timer_t.h>
#include <cstdint>
#include <cstdlib>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#define PAGE_SIZE 4096
#define CACHE_LINE_SIZE 64
#define LIKELY__(expr__) (__builtin_expect(((expr__) ? 1 : 0), 1))
#define UNLIKELY__(expr__) (__builtin_expect(((expr__) ? 1 : 0), 0))

template <typename T, size_t N> struct sizeof_is_more_than {
  constexpr static bool value = sizeof(T) >= N;
};

template <size_t N> struct power_of_2 {
  constexpr static size_t value = power_of_2<N - 1>::value << 1;
};

template <> struct power_of_2<0> {
  constexpr static size_t value = 1;
};

template <size_t N, size_t X = 0> struct next_power_of_2 {
  constexpr static size_t value = power_of_2<X>::value > N ? power_of_2<X>::value : next_power_of_2<N, X + 1>::value;
};

template <size_t N> struct next_power_of_2<N, 63> {
  constexpr static size_t value = power_of_2<63>::value;
};

template <typename T, size_t N> struct padding_size {
  static_assert(N == CACHE_LINE_SIZE, "N must be CACHE_LINE_SIZE");
  constexpr static size_t value =
      sizeof_is_more_than<T, N>::value ? next_power_of_2<sizeof(T)>::value - sizeof(T) : N - sizeof(T);
};

constexpr static size_t AUINT64_PADDING_SIZE = padding_size<std::atomic_uint_fast64_t, CACHE_LINE_SIZE>::value;
constexpr static size_t TIMEOUT_PADDING_SIZE = padding_size<struct timespec, CACHE_LINE_SIZE>::value;

struct alignas(CACHE_LINE_SIZE) count_t {
  std::atomic_uint_fast64_t count;
  uint8_t padding[AUINT64_PADDING_SIZE];
};
static_assert(sizeof(count_t) == CACHE_LINE_SIZE, "");
static_assert(alignof(count_t) == CACHE_LINE_SIZE, "");

struct alignas(CACHE_LINE_SIZE) cursor_t {
  std::atomic_uint_fast64_t sequence;
  uint8_t padding[AUINT64_PADDING_SIZE];
};
static_assert(sizeof(cursor_t) == CACHE_LINE_SIZE, "");
static_assert(alignof(cursor_t) == CACHE_LINE_SIZE, "");

struct alignas(CACHE_LINE_SIZE) timeout_t {
  struct timespec timeout;
  uint8_t padding[TIMEOUT_PADDING_SIZE];
};
static_assert(sizeof(timeout_t) == CACHE_LINE_SIZE, "");
static_assert(alignof(timeout_t) == CACHE_LINE_SIZE, "");
constexpr static timeout_t TIMEOUT = {{0, 1}, {0}};
constexpr static uint_fast64_t VACANT = UINT_FAST64_MAX;

template <size_t N, size_t R, typename T> struct ring_buffer {
  static_assert((N & (N - 1)) == 0, "N must be power of 2");
  constexpr static size_t SIZE_MASK = N - 1;
  constexpr static size_t ENTRY_PADDING_SIZE = padding_size<T, CACHE_LINE_SIZE>::value;

  struct alignas(CACHE_LINE_SIZE) entry {
    T value;
    uint8_t padding[ENTRY_PADDING_SIZE];
  };

  entry buffer[N];
  cursor_t tail_cursor; // reader
  cursor_t head_cursor; // writer
  cursor_t readers[R];
  cursor_t slowest_reader;

  ring_buffer() = delete;
  ring_buffer(const ring_buffer&) = delete;
  ring_buffer& operator=(const ring_buffer&) = delete;

  static ring_buffer* create() {
    ring_buffer* rb = nullptr;
    if (posix_memalign((void**)&rb, PAGE_SIZE, sizeof(ring_buffer))) {
      return nullptr;
    }
    memset(rb, 0x0, sizeof(ring_buffer));
    for (auto& r : rb->readers) {
      r.sequence = VACANT;
    }
    return rb;
  }

  entry* get(const cursor_t* cursor) noexcept {
    auto sequence = cursor->sequence.load(std::memory_order_relaxed);
    return &buffer[SIZE_MASK & sequence];
  };

  const entry* get(const cursor_t* cursor) const noexcept {
    auto sequence = cursor->sequence.load(std::memory_order_relaxed);
    return &buffer[SIZE_MASK & sequence];
  };

} __attribute__((aligned(PAGE_SIZE)));
