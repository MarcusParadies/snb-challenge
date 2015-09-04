# remove the # in the following line to enable reorg compilation (and running)
all: cruncher reorg

cruncher: cruncher.c utils.h
	/local/tools/gcc-4.9.2/bin/g++ -std=c++11 -march=native -I. -O3 -o cruncher cruncher.c 

loader: loader.c utils.h
	gcc -I. -O3 -o loader loader.c 

reorg: reorg.c utils.h
	/local/tools/gcc-4.9.2/bin/g++ -std=c++11 -march=native -I. -O3 -o reorg reorg.c
    
clean:
	rm -f loader cruncher reorg