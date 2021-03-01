MEINE:

nrThreads=1
Number of times we saw nothing on the queue: 52715623
Total time: 9126436881 ns for 100000000 items, which is 91.2644 ns/item
Stats: 142.494 100000000

nrThreads=2
Number of times we saw nothing on the queue: 42624
Total time: 19981300807 ns for 200000000 items, which is 99.9065 ns/item
Stats: 104086 1000369482

nrThreads=3
Number of times we saw nothing on the queue: 61794
Total time: 38039488429 ns for 300000000 items, which is 126.798 ns/item
Stats: 62672.2 2502453551

nrThreads=4
Number of times we saw nothing on the queue: 75995
Total time: 69958457624 ns for 400000000 items, which is 174.896 ns/item
Stats: 42299.9 4952133432


AtomicQueue:

nrThreads=1
Number of times we saw nothing on the queue: 9675850
Total time: 6061942912 ns for 100000000 items, which is 60.6194 ns/item
neunhoef@tux ~/c/lockfree>

nrThreads=2
Number of times we saw nothing on the queue: 3448017
Total time: 13548052485 ns for 200000000 items, which is 67.7403 ns/item

nrThreads=3
Number of times we saw nothing on the queue: 59790
Total time: 28085393905 ns for 300000000 items, which is 93.618 ns/item

nrThreads=4
Number of times we saw nothing on the queue: 1078856
Total time: 47923788019 ns for 400000000 items, which is 119.809 ns/item


