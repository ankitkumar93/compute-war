all:	test.o file.o threadpool.o
	g++ -o test test.o file.o threadpool.o -ltbb -lboost_thread -lboost_system

test.o: test.cpp
	g++ -o test.o -c test.cpp --std=c++14

file.o: file.cpp
	g++ -o file.o -c file.cpp --std=c++14

threadpool.o: threadpool.cpp
	g++ -o threadpool.o -c threadpool.cpp --std=c++14

clean:
	rm *.o test