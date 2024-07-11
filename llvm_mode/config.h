
/*
   Settings for the handling of buffer monitoring and the shared memory locations
   used by BufferMonitor
*/

// Size of shared memory region for buffer data created by BufferMonitor pass
#define SHARED_MEM_SIZE 30000 // 30 KB

// Size of one buffer data chunk
#define CHUNK_SIZE (sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint64_t))

// How many buffer data chunks fit in shared memory location
#define BUFFER_DATA_CHUNK_COUNT (SHARED_MEM_SIZE / CHUNK_SIZE)