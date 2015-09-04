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
#include <bitset>

#include "Timer.h"

using std::bitset;
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

FILE *person_ids_out;
FILE *person_bdays_out;
FILE *csr_offset_out;
FILE *csr_out;
FILE *interests_offset_out;
FILE *interests_out;

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

Person *person_map;
unsigned int *knows_map;
unsigned short *interest_map;

unsigned long person_length, knows_length, interest_length;

const uint16_t WIDTH = 16;

uint16_t computeBloomFilter(const vector<uint16_t>& artists) {
    uint16_t tmp = 0;
    for (const auto& e : artists) {
        tmp |= 1 << (e % WIDTH);
    }
    return tmp;
}

bool contains(const uint16_t val, const uint16_t bloom) {
    auto bit = (bloom >> (val % WIDTH)) & 1;
    return bit;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: [datadir] \n");
        exit(1);
    }
    /* memory-map files created by loader */
    person_map   = (Person *) mmapr(makepath(argv[1], "person",   "bin"), &person_length);
    interest_map = (unsigned short *) mmapr(makepath(argv[1], "interest", "bin"), &interest_length);
    knows_map    = (unsigned int *)   mmapr(makepath(argv[1], "knows", "bin"), &knows_length);
    
    // output files
    char* person_ids_output_file   = makepath(".", "person_ids",   "bin");
    
    char* person_bdays_output_file   = makepath(".", "person_birthdays",   "bin");
    char* csr_offset_output_file   = makepath(".", "csr_offsets",   "bin");
    char* csr_output_file   = makepath(".", "csr",   "bin");
    char* interests_output_file   = makepath(".", "interests",   "bin");
    char* interests_offset_output_file   = makepath(".", "interests_offsets",   "bin");
    
    person_ids_out = open_binout(person_ids_output_file);
    person_bdays_out = open_binout(person_bdays_output_file);
    
    csr_out = open_binout(csr_output_file);
    csr_offset_out = open_binout(csr_offset_output_file);
    
    interests_out = open_binout(interests_output_file);
    interests_offset_out = open_binout(interests_offset_output_file);

    vector<Person> p;
    Person per;
    
    utils::Timer ti;
    ti.start();
    
    utils::Timer t1;
    t1.start();
    unsigned int person_offset;
    for (person_offset = 0; person_offset < person_length/sizeof(Person); person_offset++) {
        per = (person_map[person_offset]);
        //cout << per.birthday << endl;
        p.push_back(per);
    }
    t1.stop();
    //cout << "Creating temporary person vector: " << t1.getMilliSeconds() << " ms." << endl;
    
    utils::Timer t2;
    t2.start();
    std::sort(p.begin(),p.end(), person_sort());
    t2.stop();
    //cout << "Sorting temporary person vector: " << t2.getMilliSeconds() << " ms." << endl;
    
    utils::Timer t3;
    t3.start();
    
    // csr stuff
    unsigned long knows_offset;
    uint32_t off = 0;
    fwrite(&off, sizeof(uint32_t), 1, csr_offset_out);
    
    utils::Timer t_t;
    t_t.start();
    
    // create logical consecutive person ids
    uint32_t logicalId = 0;
    unordered_map<uint64_t,uint32_t> personIdMap;
    for (const auto& e : p) {
        personIdMap.insert({e.person_id,logicalId});
        ++logicalId;
    }
    
    for (const auto& e : p) {
        // now create csr
        for (knows_offset = e.knows_first; knows_offset < e.knows_first + e.knows_n; knows_offset++) {
            Person* knows = &person_map[knows_map[knows_offset]];
            if (e.location == knows->location) {
                ++off;
                const auto it = personIdMap.find(knows->person_id);
                if (it != personIdMap.end()) {
                    fwrite(&(it->second), sizeof(uint32_t), 1, csr_out);
                }
            }
        }
        fwrite(&off, sizeof(uint32_t), 1, csr_offset_out);
    }
    
    off = 0;
    fwrite(&off, sizeof(uint32_t), 1, interests_offset_out);
    unsigned long interests_offset;
    vector<uint16_t> bloomFilters;
    bloomFilters.reserve(person_length/sizeof(Person));
    for (const auto& e : p) {
        // now create interests
        vector<uint16_t> insts;
        for (interests_offset = e.interests_first; interests_offset < e.interests_first + e.interest_n; interests_offset++) {
            uint16_t* interest = &interest_map[interests_offset];

            fwrite(&(*interest), sizeof(uint16_t), 1, interests_out);
            insts.push_back(*interest);
            ++off;
        }
        bloomFilters.push_back(computeBloomFilter(insts));
        fwrite(&off, sizeof(uint32_t), 1, interests_offset_out);
    }
    
    // now patch the person Ids
    uint32_t i = 0;
    for (const auto& e : p) {
        uint16_t bloom = bloomFilters[i];
        uint64_t tmp1 = e.person_id;
        uint64_t tmp=0;
        tmp = tmp | bloom;
        tmp = tmp << 48;
        tmp1 = tmp1 | tmp;
        //fwrite(&e.person_id, sizeof(uint64_t), 1, person_ids_out);
        fwrite(&tmp1, sizeof(uint64_t), 1, person_ids_out);
        fwrite(&e.birthday, sizeof(unsigned short), 1, person_bdays_out);
    }
    fclose(person_ids_out);
    fclose(person_bdays_out);
    
    //cout << "Number of bloom filters: " << bloomFilters.size() << endl;
    t_t.stop();
    //cout << "Elapsed: " << t_t.getMilliSeconds() << endl;
    
    fclose(csr_out);
    fclose(csr_offset_out);
    fclose(interests_out);
    fclose(interests_offset_out);
    t3.stop();
    //cout << "Hashing and filling person structure: " << t3.getMilliSeconds() << " ms." << endl;
    
    ti.stop();
    //cout << "Creating person structure: " << ti.getMilliSeconds() << " ms." << endl;
    
    
    return 0;
}