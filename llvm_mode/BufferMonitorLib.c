#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>

#include "config.h"
#include "HashMap.h"

/*
    This file contains everything related to the shared memory containing the buffer data.
*/

#ifndef WRITE_BUFFER_DATA_TO_FILE

    /*
    Layout of shared memory:
    | Buffer ID (uint32_t) | GEP ID (uint64_t) | Distance (uint64_t) | Buffer ID | GEP ID | Distance | ...
    */

    // Pointer to shared memory location
    char* __shared_memory_ = NULL;

#endif

// Hashmap for mapping buffer addresses to buffer IDs
hash_map_t* __buffer_id_map_;

/* 
    Stores a buffer in shared memory at location id * CHUNK_SIZE.
    If the new created buffer was created with the realloc function, we have to delte the old
    buffer data.
*/

void store_buffer(uint32_t buffer_id, void* buffer_address, uint64_t buffer_size, uint64_t is_realloc_function_call)
{
    
    if (is_realloc_function_call)
    {
        /* Delete old buffer data if buffer was created with realloc function. */ 
        remove_node(__buffer_id_map_, buffer_address);
    }

    /* Check if buffer with this address already exists. */
    if (get_buffer_data(__buffer_id_map_, buffer_address).buffer_id != 0)
    {
        /* If a buffer with this address already exists, we delete the old one. */
        remove_node(__buffer_id_map_, buffer_address);
    }

    insert_node(__buffer_id_map_, buffer_address, buffer_id, buffer_address, buffer_size, 0);
}

/*
    To track every buffer access, we also have to store pointers that point to the buffer.
*/

void store_buffer_pointer(uint32_t buffer_id, void* buffer_address, void* ptr_address, uint64_t accessed_byte) 
{
    /* Get buffer size. */
    uint64_t buffer_size = get_buffer_data(__buffer_id_map_, buffer_address).buffer_size;

    /* Calculate the size of the buffer that the pointer ponits to. */
    uint64_t size = buffer_size - accessed_byte;

    /* Store the buffer pointer. */
    insert_node(__buffer_id_map_, ptr_address, buffer_id, ptr_address, size, 0);
}

/* 
    Updates the accessed byte of a buffer with id 'buffer_id' if accessed_byte is greater 
    then the currently set accessed byte 
*/

void update_buffer(uint64_t getelementptr_id, void* buffer_address, uint64_t accessed_byte)
{
    update_node(__buffer_id_map_, buffer_address, getelementptr_id, accessed_byte);
}

#ifndef WRITE_BUFFER_DATA_TO_FILE

/*
    Store buffer data that is stored in the '__buffer_id_map_' into shared memory
*/

void store_buffer_data_shm()
{
    uint32_t shared_memory_offset = 0;

    /* Check if shared memory is emtpy, else find empty place. */
    uint32_t empty_offset = 0;
    while (shared_memory_offset < SHARED_MEM_SIZE) 
    {
        uint32_t buffer_id = 0;
        uint64_t gep_id = 0;
        int64_t distance = 0;

        memcpy(&buffer_id, __shared_memory_ + empty_offset, sizeof(uint32_t));
        memcpy(&gep_id, __shared_memory_ + empty_offset + sizeof(uint32_t), sizeof(uint64_t));
        memcpy(&distance, __shared_memory_ + empty_offset + sizeof(uint32_t) + sizeof(uint64_t), sizeof(int64_t));

        if (buffer_id == 0 && gep_id == 0 && distance == 0)
        {
            shared_memory_offset = empty_offset;
            break;
        }

        empty_offset += CHUNK_SIZE;
    }

    for (int i = 0; i < HASH_MAP_SIZE; i++)
    {
        node_t* current_node = __buffer_id_map_->buckets[i];

        while (current_node != NULL)
        {
            BufferInfo buffer_info = current_node->value;

            uint32_t buffer_id = buffer_info.buffer_id;
            void* buffer_address = buffer_info.buffer_address;
            uint64_t buffer_size = buffer_info.buffer_size;
            gep_instruction* current_gep_instruction = buffer_info.gep_instructions;

            while (current_gep_instruction != NULL)
            {
                uint64_t gep_id = current_gep_instruction->gep_id;
                uint64_t accessed_byte = current_gep_instruction->accessed_byte;
                
                /* If size of buffer is zero (because it is unknown) we use 1000 as size value. */
                if (buffer_size == 0)
                {
                    buffer_size = 10000;
                }

                // Caluclate the distance of the buffer defined as: buffer_size - accessed_byte
                int64_t distance = (int64_t)buffer_size - (int64_t)accessed_byte;

                // Check if buffer data fits into shared memory
                if (shared_memory_offset + CHUNK_SIZE >= SHARED_MEM_SIZE)
                {
                    return;
                }

                /* Store buffer data in shared memory. */

                /* Store buffer ID */
                memcpy(__shared_memory_ + shared_memory_offset, &buffer_id, sizeof(uint32_t));
                shared_memory_offset += sizeof(uint32_t);
                
                /* Store gep id */
                memcpy(__shared_memory_ + shared_memory_offset, &gep_id, sizeof(uint64_t));
                shared_memory_offset += sizeof(uint64_t);
                
                /* Store distance */
                memcpy(__shared_memory_ + shared_memory_offset, &distance, sizeof(int64_t));
                shared_memory_offset += sizeof(int64_t);

                current_gep_instruction = current_gep_instruction->next_gep_instruction;
            }

            current_node = current_node->next;
        }
    
    }
}
#endif

void print_hash_map()
{
    for (int i = 0; i < HASH_MAP_SIZE; i++)
    {
        node_t* current_node = __buffer_id_map_->buckets[i];

        while (current_node != NULL)
        {
            BufferInfo buffer_info = current_node->value;

            uint32_t buffer_id = buffer_info.buffer_id;
            void* buffer_address = buffer_info.buffer_address;
            uint64_t buffer_size = buffer_info.buffer_size;
            gep_instruction* current_gep_instruction = buffer_info.gep_instructions;

            while (current_gep_instruction != NULL)
            {
                uint64_t gep_id = current_gep_instruction->gep_id;
                uint64_t accessed_byte = current_gep_instruction->accessed_byte;

                printf("Buffer ID: %d, GEP ID: %lu, Buffer Address: %p, Buffer Size: %lu, Accessed Byte: %lu\n", buffer_id, gep_id, buffer_address, buffer_size, accessed_byte);

                current_gep_instruction = current_gep_instruction->next_gep_instruction;
            }

            current_node = current_node->next;
        }
    }

}

/*
    Write buffer data to a log file
*/

void log_buffer_data()
{
    FILE* log_file = fopen("./buffer_data.log", "w");

    if (log_file == NULL)
    {
        perror("Error opening file buffer_data.log");
        return;
    }

    for (int i = 0; i < HASH_MAP_SIZE; i++)
    {
        node_t* current_node = __buffer_id_map_->buckets[i];

        while (current_node != NULL)
        {
            BufferInfo buffer_info = current_node->value;
            uint32_t buffer_id = buffer_info.buffer_id;
            void* buffer_address = buffer_info.buffer_address;
            uint64_t buffer_size = buffer_info.buffer_size;
            gep_instruction* current_gep_instruction = buffer_info.gep_instructions;

            while (current_gep_instruction != NULL)
            {
                uint64_t gep_id = current_gep_instruction->gep_id;
                uint64_t accessed_byte = current_gep_instruction->accessed_byte;

                fprintf(log_file, "buffer_id: %d, gep_id: %lu, buffer_address: %p, buffer_size: %lu, accessed_byte: %lu\n", buffer_id, gep_id, buffer_address, buffer_size, accessed_byte);

                current_gep_instruction = current_gep_instruction->next_gep_instruction;
            }

            current_node = current_node->next;
        }
    }

    fclose(log_file);
}

// Constructor funtction (runs before main function)
__attribute__((constructor)) void buffer_monitor_constructor(void) 
{
    // Create hash map
    __buffer_id_map_ = create_hash_map();
    
#ifndef WRITE_BUFFER_DATA_TO_FILE

    // Create shared memory before running target program
    key_t key = ftok("/bin/clang", 1);
    int shmid = shmget(key, SHARED_MEM_SIZE, IPC_CREAT | 0666);
    if (shmid < 0) 
    {
        perror("shmget");
        return;
    }

    // Attach shared memory
    __shared_memory_ = (char*) shmat(shmid, NULL, 0);
    if (__shared_memory_ == (char *) -1) 
    {
        perror("shmat");
        return;
    }
#endif
}

// Destructor function (runs after main function)
__attribute__((destructor)) void buffer_monitor_destructor(void) 
{
#ifndef WRITE_BUFFER_DATA_TO_FILE
    store_buffer_data_shm();
    
    // Detach shared memory
    if (shmdt(__shared_memory_) == -1) 
    {
        perror("shmdt");
        return;
    }
#else
    log_buffer_data();
#endif

    // Delete hash map
    free_hash_map(__buffer_id_map_);    
}