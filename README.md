# Disruptor in C++11

This is an opinionated C++11 rewrite of [disruptorC](https://github.com/colding/disruptorC.git)
it copies the core design, does a few simplifications and fixes some performance issues. The
same test suit indicates that it outperforms by about 10% on the same hardware.

On the other hand, [DisruptorCpp](https://github.com/Abc-Arbitrage/Disruptor-cpp) reads too much
like Java code which I failed to appreciate the ergonomics, so that I intend to avoid and 
didn't do any comparisons performance wise.
