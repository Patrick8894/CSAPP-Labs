#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include "cachelab.h"

#define MAX_FILENAME_LEN 256
#define MAX_LINE_LEN 1024

// Global variables
int verbose = 0;
int num_sets, num_sets_bits, set_size, block_size;
char trace_filename[MAX_FILENAME_LEN];

int hits = 0, misses = 0, evictions = 0;

// Struct definitions
typedef struct {
    int valid;
    uint64_t tag;
    int timestamp;
} cache_line;

typedef struct {
    cache_line *lines;
} cache_set;

typedef struct {
    cache_set *sets;
} cache;

// Global cache variable
cache sim_cache;

// Function to print usage and exit
void print_usage_and_exit() {
    fprintf(stderr, "Usage: ./csim [-v] -s <s> -E <E> -b <b> -t <tracefile>\n");
    exit(EXIT_FAILURE);
}

// Function to parse and validate arguments
void parse_arguments(int argc, char *argv[]) {
    int opt;
    char *endptr;

    while ((opt = getopt(argc, argv, "vs:E:b:t:")) != -1) {
        switch (opt) {
            case 'v':
                verbose = 1;
                break;
            case 's':
                num_sets_bits = strtol(optarg, &endptr, 10);
                if (*endptr != '\0' || num_sets_bits <= 0 || num_sets_bits > 64) {
                    fprintf(stderr, "Invalid value for -s: %s\n", optarg);
                    print_usage_and_exit();
                }
                num_sets = 1 << num_sets_bits; // 2^num_set_bits
                break;
            case 'E':
                set_size = strtol(optarg, &endptr, 10);
                if (*endptr != '\0' || set_size <= 0 || set_size > 64) {
                    fprintf(stderr, "Invalid value for -E: %s\n", optarg);
                    print_usage_and_exit();
                }
                break;
            case 'b':
                block_size = strtol(optarg, &endptr, 10);
                if (*endptr != '\0' || block_size <= 0 || block_size > 64) {
                    fprintf(stderr, "Invalid value for -b: %s\n", optarg);
                    print_usage_and_exit();
                }
                break;
            case 't':
                strncpy(trace_filename, optarg, MAX_FILENAME_LEN);
                trace_filename[MAX_FILENAME_LEN - 1] = '\0';
                break;
            default:
                print_usage_and_exit();
        }
    }

    // Check if all required arguments are provided
    if (num_sets == 0 || set_size == 0 || block_size == 0 || trace_filename[0] == '\0') {
        fprintf(stderr, "Missing required arguments\n");
        print_usage_and_exit();
    }
}

// Function to initialize the cache
void init_cache() {
    sim_cache.sets = (cache_set *)malloc(num_sets * sizeof(cache_set));
    for (int i = 0; i < num_sets; i++) {
        sim_cache.sets[i].lines = (cache_line *)malloc(set_size * sizeof(cache_line));
        for (int j = 0; j < set_size; j++) {
            sim_cache.sets[i].lines[j].valid = 0;
            sim_cache.sets[i].lines[j].tag = 0;
            sim_cache.sets[i].lines[j].timestamp = 0;
        }
    }
}

// Function to free the cache
void free_cache() {
    for (int i = 0; i < num_sets; i++) {
        free(sim_cache.sets[i].lines);
    }
    free(sim_cache.sets);
}

// Function to check the cache for a given address and operation
void check_cache(char operation, uint64_t address) {
    int set_index = (address >> block_size) & (num_sets - 1);
    uint64_t tag = address >> (block_size + num_sets_bits);
    cache_set *set = &sim_cache.sets[set_index];
    int hit = 0;
    int eviction = 0;

    // Check for a hit
    for (int i = 0; i < set_size; i++) {
        if (set->lines[i].valid && set->lines[i].tag == tag) {
            hit = 1;
            hits++;
            set->lines[i].timestamp = 0; // Reset timestamp for LRU
            break;
        }
    }

    if (!hit) {
        misses++;
        // Find an empty line or the least recently used line
        int lru_index = -1;
        int max_timestamp = 0;
        for (int i = 0; i < set_size; i++) {
            if (!set->lines[i].valid) {
                lru_index = i;
                break;
            }
            if (set->lines[i].timestamp >= max_timestamp) {
                lru_index = i;
                max_timestamp = set->lines[i].timestamp;
            }
        }

        if (set->lines[lru_index].valid) {
            evictions++;
            eviction = 1;
        }

        // Update the cache line
        set->lines[lru_index].valid = 1;
        set->lines[lru_index].tag = tag;
        set->lines[lru_index].timestamp = 0; // Reset timestamp for LRU
    }

    // Update timestamps for LRU
    for (int i = 0; i < set_size; i++) {
        if (set->lines[i].valid) {
            set->lines[i].timestamp++;
        }
    }

    if (operation == 'M') {
        hits++; // Modify operation results in an additional hit
    }

    // Log the result if verbose mode is enabled
    if (verbose) {
        printf("%c %lx %s%s\n", operation, address,
               hit ? "hit" : "miss",
               eviction ? " eviction" : "");
    }
}

int main(int argc, char *argv[]) {
    // Parse and validate arguments
    parse_arguments(argc, argv);

    // Initialize the cache
    init_cache();

    // Open the trace file
    FILE *trace_file = fopen(trace_filename, "r");
    if (trace_file == NULL) {
        perror("Error opening trace file");
        exit(EXIT_FAILURE);
    }

    // Parse each line in the trace file
    char line[MAX_LINE_LEN];
    char operation;
    uint64_t address;
    int size;

    while (fgets(line, MAX_LINE_LEN, trace_file) != NULL) {
        if (sscanf(line, " %c %lx,%d", &operation, &address, &size) == 3) {
            if (operation == 'I') {
                continue; // Skip instruction cache accesses
            }

            // Check the cache for the given address and operation
            check_cache(operation, address);
        }
    }

    // Close the trace file
    fclose(trace_file);

    // Free the cache memory
    free_cache();

    printSummary(hits, misses, evictions);

    return 0;
}