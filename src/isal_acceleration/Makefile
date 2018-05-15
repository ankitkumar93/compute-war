all:	test.o file.o threadpool.o hash.o
	g++ -o test test.o file.o threadpool.o hash.o -ltbb -lboost_thread -lboost_system -lisal_crypto -lssl -lcrypto

test.o: test.cpp
	g++ -o test.o -c test.cpp --std=c++14 -O2

file.o: file.cpp
	g++ -o file.o -c file.cpp --std=c++14 -O2

threadpool.o: threadpool.cpp
	g++ -o threadpool.o -c threadpool.cpp --std=c++14 -O2

hash.o: hash.cpp
	g++ -o hash.o -c hash.cpp --std=c++14 -O2

clean:
	rm *.o test