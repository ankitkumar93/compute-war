all: experiment.o
	g++ -o experiment experiment.o -lisal -L ../lib -llzf -llzo2 -lz -lbz2 -llz4 -llzma -lsnappy

experiment.o:
	g++ -o experiment.o -c experiment.cpp --std=c++14 -O2 -march=native -I ../include/

clean:
	rm *.o experiment
