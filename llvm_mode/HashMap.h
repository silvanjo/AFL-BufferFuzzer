#ifndef HASH_MAP_H
#define HASH_MAP_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define HASH_MAP_SIZE 500

/*
    Hash map implementation for mapping buffer addresses to buffer IDs
    This is used to find the buffer ID of a buffer given its address
*/

typedef struct gep_instruction {
    uint64_t gep_id;
    uint64_t accessed_byte;

    struct gep_instruction* next_gep_instruction;
} gep_instruction;

/*
This is all the data we store about the buffer.
*/

typedef struct BufferInfo {
    uint32_t buffer_id;
    void* buffer_address;
    uint64_t buffer_size;
    gep_instruction* gep_instructions;
} BufferInfo;

typedef struct map_node
{
    void* key;
    BufferInfo value;

    struct map_node* next;
} node_t;

typedef struct hash_map
{
    node_t* buckets[HASH_MAP_SIZE];
} hash_map_t;

hash_map_t* create_hash_map();

void free_hash_map(hash_map_t* map);

/*
    The key of the hash map is the address of the buffer
*/

uint32_t hash_function(void* key);

BufferInfo get_buffer_data(hash_map_t* map, void* key);

void insert_node(hash_map_t* map, void* key, uint32_t buffer_id, void* buffer_address, uint64_t buffer_size, uint64_t accessed_byte);

/*
    Update the accessed byte of a buffer if the new accessed byte is greater than the current accessed byte.
    Returns true if the accessed byte was updated, false otherwise.
*/

uint8_t update_node(hash_map_t* map, void* key, uint64_t getelementptr_id, uint64_t accessed_byte);

void remove_node(hash_map_t* map, void* key);

#endif // HASH_MAP_H