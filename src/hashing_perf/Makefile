all:	test.o file.o directory.o threadpool.o hash.o
	g++ -o test test.o file.o directory.o threadpool.o hash.o -ltbb -lboost_thread -lboost_system -lboost_filesystem -lisal_crypto -lssl -lcrypto -L skein -lskein

test.o: test.cpp
	g++ -o test.o -c test.cpp --std=c++14 -O2 -march=native

file.o: file.cpp
	g++ -o file.o -c file.cpp --std=c++14 -O2 -march=native

directory.o: directory.cpp
	g++ -o directory.o -c directory.cpp --std=c++14 -O2 -march=native

threadpool.o: threadpool.cpp
	g++ -o threadpool.o -c threadpool.cpp --std=c++14 -O2 -march=native

hash.o: hash.cpp
	g++ -o hash.o -c hash.cpp --std=c++14 -O2 -march=native

clean:
	rm *.o test