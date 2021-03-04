n threads push pointers to a queue of size 1048576
1 thread pops them as fast as it can
All times in nanoseconds per operation, time is computed by measuring
the total time in the consumer and dividing by the number of pops,
so this is throughput.

NrThreads:                      1       2       4       7       8       16
=============================================================================
SingleConsumer, gcc             22      37      29      ?       27      31
SingleConsumer, clang12         21      37      33      ?       28      33
SingleConsumer, clang11         21      38      35      ?       28      39
=============================================================================
AtomicQueue, gcc                67      69      115     ?      134     313
AtomicQueue, clang12            55      68      79      ?      125     281
AtomicQueue, clang11            55      68      106     ?      132     247
=============================================================================
BoostLockFree, gcc              138     152     196     ?      197     198
BoostLockFree, clang12          144     152     203     ?      200     196
BoostLockFree, clang11          137     149     210     ?      202     204
==new version================================================================
SingleConsumer, gcc             11      27      26      24      22      24
SingleConsumer, clang12         11      28      26      24      21      22
SingleConsumer, clang11         10      27      27      24      22      22



Latency:

> build/condvarlatency
nrThreads=1
Latencies: median=45262 90%ile=57594 99%ile=71078 smallest=3775
largest 10: 64176 65004 65291 65301 65306 67954 68019 69105 71078 90800

> build/lockfreelatency
nrThreads=1
Latencies: median=44760 90%ile=60226 99%ile=74059 smallest=3223
largest 10: 65700 67697 68304 68469 70259 70358 71845 72933 74059 88654
Number of sleeps: 200


