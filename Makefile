debug: clean
	echo >> benchresults.txt
	echo debug >> benchresults.txt
	mkdir build
	cd build ; cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-march="native -O0" .. ; cmake --build . -- -j

gcc: clean
	echo >> benchresults.txt
	echo gcc >> benchresults.txt
	mkdir build
	cd build ; cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-march="native -O3" .. ; cmake --build . -- -j

clang12: clean
	echo >> benchresults.txt
	echo clang12 >> benchresults.txt
	mkdir build
	cd build ; cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-march="native -O3" -DCMAKE_C_COMPILER=/usr/lib/ccache/clang-12 -DCMAKE_CXX_COMPILER=/usr/lib/ccache/clang++-12 -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld .. ; cmake --build . -- -j

clang11: clean
	echo >> benchresults.txt
	echo clang11 >> benchresults.txt
	mkdir build
	cd build ; cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-march="native -O3" -DCMAKE_C_COMPILER=/usr/lib/ccache/clang-11 -DCMAKE_CXX_COMPILER=/usr/lib/ccache/clang++-11 -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld-11 .. ; cmake --build . -- -j

bench:
	build/lockfree 1 30000000 | tee -a benchresults.txt
	build/lockfree 2 30000000 | tee -a benchresults.txt
	build/lockfree 4 30000000 | tee -a benchresults.txt
	build/lockfree 7 30000000 | tee -a benchresults.txt
	build/lockfree 8 30000000 | tee -a benchresults.txt
	build/lockfree 16 30000000 | tee -a benchresults.txt

clean:
	rm -rf build
