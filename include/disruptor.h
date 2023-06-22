#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <atomic>
#include <utility>

#define PAGE_SIZE 4096
#define CACHE_LINE_SIZE 64
#define LIKELY__(expr__) (__builtin_expect(((expr__) ? 1 : 0), 1))
#define UNLIKELY__(expr__) (__builtin_expect(((expr__) ? 1 : 0), 0))

template <typename T, size_t N>
struct sizeof_is_more_than {
    constexpr static bool value = sizeof(T) >= N;
};

template <size_t N>
struct power_of_2 {
    constexpr static size_t value = power_of_2<N - 1>::value << 1;
};

template <>
struct power_of_2<0> {
    constexpr static size_t value = 1;
};

template <size_t N, size_t X = 0>
struct next_power_of_2 {
    constexpr static size_t value = power_of_2<X>::value > N ? power_of_2<X>::value : next_power_of_2<N, X + 1>::value;
};

// reasonable hack
template <size_t N>
struct next_power_of_2<N, 63> {
    constexpr static size_t value = power_of_2<63>::value;
};

template <typename T, size_t N>
struct padding_size {
    static_assert(N == CACHE_LINE_SIZE, "N must be CACHE_LINE_SIZE");
    constexpr static size_t value =
        sizeof_is_more_than<T, N>::value ? next_power_of_2<sizeof(T)>::value - sizeof(T) : N - sizeof(T);
};

constexpr static size_t AUINT64_PADDING_SIZE = padding_size<std::atomic_uint_fast64_t, CACHE_LINE_SIZE>::value;
constexpr static size_t TIMEOUT_PADDING_SIZE = padding_size<struct timespec, CACHE_LINE_SIZE>::value;

struct alignas(CACHE_LINE_SIZE) count_t {
    std::atomic_uint_fast64_t count{0};
    uint8_t padding[AUINT64_PADDING_SIZE];
};
static_assert(sizeof(count_t) == CACHE_LINE_SIZE, "count_t is 1 CACHE_LINE_SIZE");
static_assert(alignof(count_t) == CACHE_LINE_SIZE, "");

struct alignas(CACHE_LINE_SIZE) cursor_t {
    std::atomic_uint_fast64_t sequence{0};
    uint8_t padding[AUINT64_PADDING_SIZE];
};
static_assert(sizeof(cursor_t) == CACHE_LINE_SIZE, "cursor_t is 1 CACHE_LINE_SIZE");
static_assert(alignof(cursor_t) == CACHE_LINE_SIZE, "");

struct alignas(CACHE_LINE_SIZE) timeout_t {
    struct timespec timeout;
    uint8_t padding[TIMEOUT_PADDING_SIZE];
};
static_assert(sizeof(timeout_t) == CACHE_LINE_SIZE, "");
static_assert(alignof(timeout_t) == CACHE_LINE_SIZE, "");
constexpr static timeout_t TIMEOUT = {{0, 1}, {0}};
constexpr static uint_fast64_t VACANT = UINT_FAST64_MAX;

template <size_t N, size_t R, typename T>
struct ring_buffer {
    static_assert((N & (N - 1)) == 0, "N must be power of 2");
    constexpr static size_t SIZE_MASK = N - 1;
    constexpr static size_t ENTRY_PADDING_SIZE = padding_size<T, CACHE_LINE_SIZE>::value;
    constexpr static size_t READER_PADDING_SIZE =
        padding_size<std::atomic_uint64_t, CACHE_LINE_SIZE>::value - sizeof(uint_fast64_t) - sizeof(ring_buffer *);

    struct alignas(CACHE_LINE_SIZE) entry {
        T value;
        uint8_t padding[ENTRY_PADDING_SIZE];
    };

    // each reader thread writes its own begin and end
    // readers check ring buffer's tail_cursor to catch up to it (including)
    // in other words, tail_cursor is the last readable entry
    struct alignas(CACHE_LINE_SIZE) reader {
        std::atomic_uint_fast64_t end;
        uint_fast64_t begin;
        ring_buffer *rb;
        uint8_t padding[READER_PADDING_SIZE];

        template <typename F, typename... Args>
        void block_read(F &&f, Args &&...args) {
            uint_fast64_t b = this->begin;
            uint_fast64_t e = this->end;
            while (e > this->rb->tail_cursor.sequence.load(std::memory_order_relaxed)) {
                nanosleep(&TIMEOUT.timeout, NULL);
            }
            e = this->rb->tail_cursor.sequence.load(std::memory_order_acquire);
            for (uint_fast64_t n = b; n <= e; n++) {
                f(this->rb->get(n), std::forward<Args>(args)...);
            }
            this->begin = e + 1;
            this->end.store(this->begin, std::memory_order_relaxed);
        }
    };
    static_assert(sizeof(reader) == CACHE_LINE_SIZE, "reader is CACHE_LINE_SIZE");

    entry buffer[N];
    reader readers[R];  // writers check each reader's end, writers can only write
                        // when the last_reader catch up close enough
    count_t reader_num;
    cursor_t tail_cursor;  // writers contend to commit tail_cursor
    cursor_t head_cursor;  // writers contend to fetch head_cursor, (last_reader,
                           // head_cursor) must be in ring buffer's range

    ring_buffer() = delete;
    ring_buffer(const ring_buffer &) = delete;
    ring_buffer &operator=(const ring_buffer &) = delete;

    // writers can only write when ring buffer is ready
    bool ready() const noexcept { return this->reader_num.count == R; }

    static ring_buffer *create() {
        ring_buffer *rb = nullptr;
        if (posix_memalign((void **)&rb, PAGE_SIZE, sizeof(ring_buffer))) {
            return nullptr;
        }
        memset((void *)rb, 0x0, sizeof(ring_buffer));
        for (auto &r : rb->readers) {
            r.end = VACANT;
        }
        return rb;
    }

    entry *get(uint_fast64_t sequence) noexcept { return &buffer[SIZE_MASK & sequence]; };
    const entry *get(uint_fast64_t sequence) const noexcept { return &buffer[SIZE_MASK & sequence]; };

    reader *create_reader() {
        uint_fast64_t vacant = VACANT;
        while (reader_num.count.load(std::memory_order_acquire) < R) {
            for (auto &r : this->readers) {
                if (r.end.compare_exchange_weak(vacant, 1, std::memory_order_release, std::memory_order_relaxed)) {
                    reader_num.count.fetch_add(1, std::memory_order_release);
                    r.rb = this;
                    r.begin = 1;
                    return &r;
                }
                vacant = VACANT;
            }
        }
        return nullptr;
    }

    template <typename F, typename... Args>
    void spin_write(F &&f, Args &&...args) {
        uint_fast64_t last_reader;
        uint_fast64_t next_header;
        while (true) {  // spin
            next_header = 1 + this->head_cursor.sequence.load(std::memory_order_relaxed);
            last_reader = VACANT;
            for (auto &r : this->readers) {
                uint_fast64_t last = r.end.load(std::memory_order_relaxed) - 1;
                if (last_reader > last) {
                    last_reader = last;
                }
            }
            if (UNLIKELY__(VACANT == last_reader)) {
                last_reader = next_header - (SIZE_MASK & next_header);
            }
            if (LIKELY__(next_header - last_reader <= SIZE_MASK)) {
                uint_fast64_t desired = next_header - 1;
                if (this->head_cursor.sequence.compare_exchange_weak(desired, next_header, std::memory_order_relaxed,
                                                                     std::memory_order_relaxed)) {
                    break;
                }
            }
        }
        f(get(next_header), std::forward<Args>(args)...);
        while (this->tail_cursor.sequence.load(std::memory_order_relaxed) != next_header - 1) {
            // spin
        }
        this->tail_cursor.sequence.fetch_add(1, std::memory_order_release);
    }

    template <typename F, typename... Args>
    void block_write(F &&f, Args &&...args) {
        uint_fast64_t last_reader;
        uint_fast64_t next_header = 1 + this->head_cursor.sequence.fetch_add(1, std::memory_order_relaxed);
        while (true) {
            last_reader = VACANT;
            for (auto &r : this->readers) {
                uint_fast64_t last = r.end.load(std::memory_order_relaxed) - 1;
                if (last_reader > last) {
                    last_reader = last;
                }
            }
            if (UNLIKELY__(VACANT == last_reader)) {
                last_reader = next_header - (SIZE_MASK & next_header);
            }
            if (LIKELY__(next_header - last_reader <= SIZE_MASK)) {
                f(get(next_header), std::forward<Args>(args)...);
                break;
            }
            nanosleep(&TIMEOUT.timeout, NULL);
        }

        while (this->tail_cursor.sequence.load(std::memory_order_relaxed) != next_header - 1) {
            nanosleep(&TIMEOUT.timeout, NULL);
        }
        this->tail_cursor.sequence.fetch_add(1, std::memory_order_release);
    }

} __attribute__((aligned(PAGE_SIZE)));
