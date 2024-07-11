#include "HashMap.h"

uint32_t hash_function(void* key)
{
    return (uint32_t)((uintptr_t) key % HASH_MAP_SIZE);
}

hash_map_t* create_hash_map()
{
    hash_map_t* map = (hash_map_t*) malloc(sizeof(hash_map_t));

    if (map == NULL)
    {
        perror("Error: Could not allocate memory for hash map.\n");
        return NULL;
    }

    for (int i = 0; i < HASH_MAP_SIZE; i++)
    {
        map->buckets[i] = NULL;
    }

    return map;
}

void free_hash_map(hash_map_t* map)
{
    if (map == NULL)
    {
        return;
    }

    for (int i = 0; i < HASH_MAP_SIZE; i++)
    {
        node_t* current_node = map->buckets[i];

        while (current_node != NULL)
        {
            gep_instruction* current_gep_instruction = current_node->value.gep_instructions;
            while (current_gep_instruction != NULL)
            {
                gep_instruction* next_gep_instruction = current_gep_instruction->next_gep_instruction;
                free(current_gep_instruction);
                current_gep_instruction = next_gep_instruction;
            }

            node_t* next_node = current_node->next;
            free(current_node);
            current_node = next_node;
        }
    }

    free(map);
}

BufferInfo get_buffer_data(hash_map_t* map, void* key)
{
    if (key == NULL || map == NULL)
        return (BufferInfo){0, NULL, 0, NULL};
    
    uint32_t hash = hash_function(key);

    node_t* current_node = map->buckets[hash];

    while (current_node != NULL)
    {
        if (current_node->key == key)
        {
            return current_node->value;
        }

        current_node = current_node->next;
    }

    BufferInfo buffer_info;
    buffer_info.buffer_id = 0;
    buffer_info.buffer_address = NULL;
    buffer_info.buffer_size = 0;

    return buffer_info;
}

void insert_node(hash_map_t* map, void* key, uint32_t buffer_id, void* buffer_address, uint64_t buffer_size, uint64_t accessed_byte)
{
    if (key == NULL || map == NULL)
        return;
    
    uint32_t hash = hash_function(key);

    node_t* new_node = (node_t*) malloc(sizeof(node_t));

    if (new_node == NULL)
    {
        perror("Error: Could not allocate memory for hash map node\n");
        return;
    }

    new_node->key = key;
    new_node->value.buffer_id = buffer_id;
    new_node->value.buffer_address = buffer_address;
    new_node->value.buffer_size = buffer_size;
    new_node->value.gep_instructions = NULL;
    new_node->next = NULL;

    if (map->buckets[hash] == NULL)
    {
        map->buckets[hash] = new_node;
    }
    else
    {
        node_t* current_node = map->buckets[hash];

        while (current_node->next != NULL)
        {
            current_node = current_node->next;
        }

        current_node->next = new_node;
    }
}

uint8_t update_node(hash_map_t* map, void* key, uint64_t getelementptr_id, uint64_t accessed_byte)
{
    if (accessed_byte == 0)
        return 0;
    
    uint32_t hash = hash_function(key);

    node_t* current_node = map->buckets[hash];

    while (current_node != NULL)
    {
        if (current_node->key == key)
        {
            /* 
            Iterate through the gep instructions performed on this buffer and check if the
            and check if a getelementptr instruction with the same ID has already been performed. 
            */

            gep_instruction* current_gep_instruction = current_node->value.gep_instructions;

            while (current_gep_instruction != NULL)
            {
                if (current_gep_instruction->gep_id == getelementptr_id)
                {
                    if (accessed_byte > current_gep_instruction->accessed_byte)
                    {
                        current_gep_instruction->accessed_byte = accessed_byte;
                    }

                    return 1;
                }

                current_gep_instruction = current_gep_instruction->next_gep_instruction;
            }

            gep_instruction* new_gep_instruction = (gep_instruction*) malloc(sizeof(gep_instruction));
            new_gep_instruction->gep_id = getelementptr_id;
            new_gep_instruction->accessed_byte = accessed_byte;
            new_gep_instruction->next_gep_instruction = NULL;

            if (current_node->value.gep_instructions == NULL)
            {
                current_node->value.gep_instructions = new_gep_instruction;
            } else 
            {
                // Insert new gep instruction at the beginning of the list
                new_gep_instruction->next_gep_instruction = current_node->value.gep_instructions;
                current_node->value.gep_instructions = new_gep_instruction;
            }

            return 1;
        }

        current_node = current_node->next;
    }

    return 0;
}

void remove_node(hash_map_t* map, void* key)
{
    uint32_t hash = hash_function(key);

    node_t* current_node = map->buckets[hash];
    node_t* previous_node = NULL;

    while (current_node != NULL)
    {
        if (current_node->key == key)
        {
            if (previous_node == NULL)
            {
                map->buckets[hash] = current_node->next;
            }
            else
            {
                previous_node->next = current_node->next;
            }

            /* Remove all gep instructions of this node */
            gep_instruction* current_gep_instruction = current_node->value.gep_instructions;
            while (current_gep_instruction != NULL)
            {
                gep_instruction* next_gep_instruction = current_gep_instruction->next_gep_instruction;
                free(current_gep_instruction);
                current_gep_instruction = next_gep_instruction;
            }

            free(current_node);
            return;
        }

        previous_node = current_node;
        current_node = current_node->next;
    }
}