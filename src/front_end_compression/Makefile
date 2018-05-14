all:	test.o file.o threadpool.o compress.o
	g++ -o test test.o file.o threadpool.o compress.o -llz4 -ltbb -lboost_thread -lboost_system -L . -llzf

test.o: test.cpp
	g++ -o test.o -c test.cpp --std=c++14

file.o: file.cpp
	g++ -o file.o -c file.cpp --std=c++14

threadpool.o: threadpool.cpp
	g++ -o threadpool.o -c threadpool.cpp --std=c++14

compress.o: compress.cpp
	g++ -o compress.o -c compress.cpp --std=c++14

clean:
	rm *.o test
