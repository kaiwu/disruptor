# Disruptor in C++11

This is an opinionated C++11 rewrite of [disruptorC](https://github.com/colding/disruptorC.git).
The rewrite copies the core design, does a few simplifications and fixes some performance issues. The
original test suit indicates that it outperforms by about 10% on the same hardware.

On the other hand, [DisruptorCpp](https://github.com/Abc-Arbitrage/Disruptor-cpp) reads too much
like Java code, whose ergonomics I failed to appreciate. Therefore I intend to avoid it and didn't do
any comparisons performance wise.
