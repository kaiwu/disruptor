#include <stdio.h>
#include <sys/time.h>

#include <cstddef>
#include <thread>

#include "catch.hpp"
#include "disruptor.h"

TEST_CASE("create", "[disruptor]") {
  REQUIRE(sizeof(cursor_t) == CACHE_LINE_SIZE);
  REQUIRE(sizeof(count_t) == CACHE_LINE_SIZE);

  typedef size_t A;
  using disruptor = ring_buffer<16, 2, A>;
  REQUIRE(sizeof(disruptor::entry) == CACHE_LINE_SIZE);

  size_t total = 100;
  size_t s1{0};
  size_t s2{0};
  auto rb = disruptor::create();

  auto add = [rb, total](size_t* s) {
    auto r = rb->create_reader();
    REQUIRE(r != nullptr);
    size_t n = 0;
    while (n < total * 2) {
      r->block_read([s, &n](const disruptor::entry* e) {
        *s += e->value;
        n += 1;
      });
    }
  };

  std::thread c1{add, &s1};
  std::thread c2{add, &s2};

  while (!rb->ready())
    ;

  std::thread p1{[rb, total]() {
    size_t n = 1;
    while (n <= total) {
      rb->spin_write([&n](disruptor::entry* e) {
        e->value = n;
        n += 1;
      });
    }
  }};

  std::thread p2{[rb, total]() {
    size_t n = 1;
    while (n <= total) {
      rb->block_write([&n](disruptor::entry* e) {
        e->value = n;
        n += 1;
      });
    }
  }};

  c1.join();
  c2.join();
  p1.join();
  p2.join();

  REQUIRE(5050 * 2 == s1);
  REQUIRE(5050 * 2 == s2);

  free(rb);
}

TEST_CASE("performance", "[disruptor]") {
  typedef size_t A;
  using disruptor = ring_buffer<1024 * 2, 1, A>;
  size_t total = 50 * 1000 * 1000 * 5;
  size_t sum{0};
  struct timeval start;
  struct timeval end;

  auto rb = disruptor::create();

  std::thread c1{[rb, &end, total](size_t* s) {
    auto r = rb->create_reader();
    REQUIRE(r != nullptr);
    size_t n = 0;
    while (n < total) {
      r->block_read([&n, s](const disruptor::entry* e) { 
          *s += e->value;
          n += 1; 
      });
    }
    gettimeofday(&end, NULL);
  }, &sum};

  while (!rb->ready())
    ;

  std::thread p1{[rb, &start, total]() {
    gettimeofday(&start, NULL);
    size_t n = 0;
    while (n < total) {
      rb->spin_write([&n](disruptor::entry* e) {
        e->value = 1;
        n += 1;
      });
    }
  }};

  c1.join();
  p1.join();

  REQUIRE(sum == total);
  double stime = (double)start.tv_sec + (double)start.tv_usec / 1000000.0;
  double etime = (double)end.tv_sec + (double)end.tv_usec / 1000000.0;
  double span = etime - stime;
  printf("span: %lf seconds\n", span);
  printf("per second: %lf\n", total / span);

  free(rb);
}
