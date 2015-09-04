#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <vector>
#include <unordered_map>
#include <algorithm>
#include <iostream>

#include "Timer.h"

using std::vector;
using std::unordered_map;
using std::cout;
using std::endl;

#define QUERY_FIELD_QID 0
#define QUERY_FIELD_A1 1
#define QUERY_FIELD_A2 2
#define QUERY_FIELD_A3 3
#define QUERY_FIELD_A4 4
#define QUERY_FIELD_BS 5
#define QUERY_FIELD_BE 6
#define REPORTING_N 1000000
#define LINEBUFLEN 1024

typedef unsigned long byteoffset;
typedef unsigned int  entrycount;
//typedef unsigned char bool;

FILE *person_ids_out;
FILE *person_bdays_out;
FILE *csr_offset_out;
FILE *csr_out;

void parse_csv(char* fname, void (*line_handler)(unsigned char nfields, char** fieldvals)) {
	long nlines = 0;

   	FILE* stream = fopen(fname, "r");
	if (stream == NULL) {
		fprintf(stderr, "Can't read file at %s\n", fname);
		exit(-1);
	}
	char line[LINEBUFLEN];
	char* tokens[10];
	unsigned int col, idx;
	tokens[0] = line;

	while (fgets(line, LINEBUFLEN, stream)) {
		col = 0;
		// parse the csv line into array of strings
		for (idx=0; idx<LINEBUFLEN; idx++) { 
			if (line[idx] == '|' || line[idx] == '\n') {
				line[idx] = '\0';
				col++;
				tokens[col] = &line[idx+1];
			} // lookahead to find end of line
			if (line[idx+1] == '\0') {
				break;
			}
		}
		(*line_handler)(col, tokens);
		nlines++;
		if (nlines % REPORTING_N == 0) {
			printf("%s: read %lu lines\n", fname, nlines);
		}
	}
	fclose(stream);
}

unsigned short birthday_to_short(char* date) {
	unsigned short bdaysht;
	char dmbuf[3];
	dmbuf[2] = '\0';
	dmbuf[0] = *(date + 5);
	dmbuf[1] = *(date + 6);
	bdaysht = atoi(dmbuf) * 100;
	dmbuf[0] = *(date + 8);
	dmbuf[1] = *(date + 9);
	bdaysht += atoi(dmbuf);
	return bdaysht;
}

FILE* open_binout(char* filename) {
	FILE* outfile;
	outfile = fopen(filename, "wb");
	if (outfile == NULL) {
		fprintf(stderr, "Could not open %s for writing\n", filename);
		exit(-1);
	}
	return outfile;
}

typedef struct {
    unsigned long  person_id;
    unsigned short birthday;
    unsigned short location;
    unsigned long  knows_first;
    unsigned short knows_n;
    unsigned long  interests_first;
    unsigned short interest_n;
} Person;

typedef struct {
    uint64_t person_id;
    uint64_t friend_id;
    uint16_t score;
} Result;

typedef vector<Result> ResultVec;

void* mmapopen(char* filename, byteoffset *filelen, bool write) {
    int fd;
    struct stat sbuf;
    void *mapaddr;
    int fopenmode = write ? O_RDWR : O_RDONLY;
    int mmapmode = write ? PROT_WRITE : PROT_READ;

    if ((fd = open(filename, fopenmode)) == -1) {
        fprintf(stderr, "failed to open %s\n", filename);
        exit(1);
    }

    if (stat(filename, &sbuf) == -1) {
        fprintf(stderr, "failed to stat %s\n", filename);
        exit(1);
    }
    
    mapaddr = mmap(0, sbuf.st_size, mmapmode, MAP_SHARED, fd, 0);
    if (mapaddr == MAP_FAILED) {
        fprintf(stderr, "failed to mmap %s\n", filename);
        exit(1);
    }
    *filelen = sbuf.st_size;
    return mapaddr;
}

void* mmapr(char* filename, byteoffset *filelen) {
    return mmapopen(filename, filelen, 0);
}

void* mmaprw(char* filename, byteoffset *filelen) {
    return mmapopen(filename, filelen, 1);
}

char* makepath(char* dir, char* file, char* ext) {
    char* out = (char*)malloc(1024);
    sprintf(out, "%s/%s.%s", dir, file, ext);
    return out;
}

struct person_sort {
    bool operator ()(Person const& a, Person const& b) const {
        if (a.birthday < b.birthday) return true;
        if (a.birthday >= b.birthday) return false;
    }
};

uint64_t * person_id_map;
uint16_t * person_bday_map;

uint32_t * csr_map; 
uint32_t * csr_offset_map;

uint16_t * interests_map; 
uint32_t * interests_offset_map;

unsigned long person_id_length, person_bday_length, knows_length, interest_length;
unsigned long csr_length, csr_offset_length;
unsigned long interests_length, interests_offset_length;

void query(unsigned short qid, unsigned short artist, unsigned short areltd[], unsigned short bdstart, unsigned short bdend) {
    
    // (1) get persons from birthday
    uint32_t logicalID;
    utils::Timer t;
    t.start();
    vector<uint32_t> persons;
    persons.reserve(50000);
    for (logicalID = 0; logicalID < person_id_length/sizeof(uint64_t); logicalID++) {
        unsigned short bd = person_bday_map[logicalID];
        if ((bd >= bdstart) && (bd <= bdend)) {
            persons.push_back(logicalID);
        }
    }
    t.stop();
    //cout << "Number of persons: " << persons.size() << " in " << t.getMilliSeconds() << endl;
    
    // (2) perform check for artist
    uint32_t numberOfPersons = person_id_length/sizeof(uint64_t);
    uint32_t k = 0;
    for (const auto& e : persons) {
        uint32_t begin = *&interests_offset_map[e];
        uint32_t end = *&interests_offset_map[e+1];
        uint32_t i;
        uint16_t cnt = 0;
        for (i = begin; i < end; ++i) {
            if ((interests_map[i] == artist)) {
                persons[k] = UINT32_MAX;
                break;
            }
            
            if (((interests_map[i] == areltd[0]) || (interests_map[i] == areltd[1]) || (interests_map[i] == areltd[2]))) {
                ++cnt;
            }
        }
        if (i == end && cnt == 0) {
            persons[k] = UINT32_MAX;
        }
        ++k;
    }
    
    // (3) expand traversal
    ResultVec vec;
    for (const auto& p : persons) {
        // p is still candidate
        vector<uint32_t> neighbors;
        if (p < UINT32_MAX) {
            uint32_t begin = *&csr_offset_map[p];
            uint32_t end = *&csr_offset_map[p+1];
            for (uint32_t i = begin; i < end; ++i) {
                uint32_t neighbor = *&csr_map[i];
                neighbors.push_back(neighbor);
            }
            vector<uint32_t> n;
            for (const auto& f : neighbors) {
                // p is still candidate
                uint32_t begin = *&csr_offset_map[f];
                uint32_t end = *&csr_offset_map[f+1];
                uint32_t i;
                for (i = begin; i < end; ++i) {
                    if (p == *&csr_map[i]) {
                        break;
                    }
                }
                if (i < end) {
                    uint32_t begin = *&interests_offset_map[f];
                    uint32_t end = *&interests_offset_map[f+1];
                    for (uint32_t i = begin; i < end; ++i) {
                        if (interests_map[i] == artist) {
                            Result res;
                            res.person_id = p;
                            res.friend_id = f;
                            res.score = 0;
                            vec.push_back(res);
                            break;
                        }
                    }
                }
            }
        }
    }

    for (auto& e : vec) {
        uint32_t begin = *&interests_offset_map[e.person_id];
        uint32_t end = *&interests_offset_map[e.person_id+1];
        for (uint32_t i = begin; i < end; ++i) {
            if (((interests_map[i] == areltd[0]) || (interests_map[i] == areltd[1]) || (interests_map[i] == areltd[2]))) {
                ++e.score;
                if (e.score == 3){
                    break;
                }
            }
        }
    }
    
    for (const auto& e : vec) {
        cout << e.person_id << " " << e.friend_id << " " << e.score << endl;
    }
}

void query_line_handler(unsigned char nfields, char** tokens) {
    unsigned short q_id, q_artist, q_bdaystart, q_bdayend;
    unsigned short q_relartists[3];
    
    utils::Timer t;
    t.start();

    q_id            = atoi(tokens[QUERY_FIELD_QID]);
    q_artist        = atoi(tokens[QUERY_FIELD_A1]);
    q_relartists[0] = atoi(tokens[QUERY_FIELD_A2]);
    q_relartists[1] = atoi(tokens[QUERY_FIELD_A3]);
    q_relartists[2] = atoi(tokens[QUERY_FIELD_A4]);
    q_bdaystart     = birthday_to_short(tokens[QUERY_FIELD_BS]);
    q_bdayend       = birthday_to_short(tokens[QUERY_FIELD_BE]);
    
    query(q_id, q_artist, q_relartists, q_bdaystart, q_bdayend);
    
    t.stop();
    //cout << "Elapsed time: " << t.getMilliSeconds() << " ms." << endl;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: [datadir] \n");
        exit(1);
    }
    
    /* memory-map files created by loader */
    person_id_map   = (uint64_t *) mmapr(makepath(argv[1], "person_ids", "bin"), &person_id_length);
    person_bday_map = (unsigned short *) mmapr(makepath(argv[1], "person_birthdays", "bin"), &person_bday_length);
    
    csr_map = (uint32_t *) mmapr(makepath(argv[1], "csr", "bin"), &csr_length);
    csr_offset_map = (uint32_t *) mmapr(makepath(argv[1], "csr_offsets", "bin"), &csr_offset_length);
    
    interests_map   = (uint16_t *) mmapr(makepath(argv[1], "interests", "bin"), &interests_length);
    interests_offset_map = (uint32_t *) mmapr(makepath(argv[1], "interests_offsets", "bin"), &interests_offset_length);
    
    parse_csv(argv[2], &query_line_handler);
    
    return 0;
}