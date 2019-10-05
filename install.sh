mkdir build
cd build
cmake ..
cmake --build .
cd ..
cp build/memdb /usr/bin/memdb