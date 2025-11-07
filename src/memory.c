#include "memory.h"

// Simple heap allocator
// Heap starts after BSS section and grows upward
extern char __bss_end[];  // Defined in linker script - use char array to avoid aliasing
#define HEAP_START ((uint32_t)__bss_end)
#define HEAP_SIZE  (32 * 1024 * 1024)  // 32 MB heap

typedef struct block_header {
    uint32_t size;              // Size of block (including header)
    struct block_header *next;  // Next free block
    uint32_t is_free;           // 1 if free, 0 if allocated
} block_header_t;

static block_header_t *free_list = 0;
static uint32_t heap_initialized = 0;
static memory_info_t mem_info;

void memory_init(void) {
    // Initialize the free list with one large block
    free_list = (block_header_t *)HEAP_START;
    free_list->size = HEAP_SIZE;
    free_list->next = 0;
    free_list->is_free = 1;
    
    mem_info.heap_start = HEAP_START;
    mem_info.heap_end = HEAP_START + HEAP_SIZE;
    mem_info.total = HEAP_SIZE;
    mem_info.used = sizeof(block_header_t);
    mem_info.free = HEAP_SIZE - sizeof(block_header_t);
    
    heap_initialized = 1;
}

void *malloc(uint32_t size) {
    if (!heap_initialized) memory_init();
    if (size == 0) return 0;
    
    // Align size to 8 bytes
    size = (size + 7) & ~7;
    uint32_t total_size = size + sizeof(block_header_t);
    
    // Find first fit
    block_header_t *current = free_list;
    
    while (current) {
        if (current->is_free && current->size >= total_size) {
            // Found a suitable block
            if (current->size >= total_size + sizeof(block_header_t) + 8) {
                // Split the block
                block_header_t *new_block = (block_header_t *)((uint8_t *)current + total_size);
                new_block->size = current->size - total_size;
                new_block->next = current->next;
                new_block->is_free = 1;
                
                current->size = total_size;
                current->next = new_block;
            }
            
            current->is_free = 0;
            mem_info.used += current->size;
            mem_info.free -= current->size;
            
            return (void *)((uint8_t *)current + sizeof(block_header_t));
        }
        
        current = current->next;
    }
    
    return 0;  // Out of memory
}

void free(void *ptr) {
    if (!ptr) return;
    
    block_header_t *block = (block_header_t *)((uint8_t *)ptr - sizeof(block_header_t));
    block->is_free = 1;
    
    mem_info.used -= block->size;
    mem_info.free += block->size;
    
    // Coalesce adjacent free blocks
    block_header_t *current = free_list;
    while (current && current->next) {
        if (current->is_free && current->next->is_free) {
            current->size += current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
}

void *calloc(uint32_t nmemb, uint32_t size) {
    uint32_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, uint32_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return 0;
    }
    
    block_header_t *block = (block_header_t *)((uint8_t *)ptr - sizeof(block_header_t));
    uint32_t old_size = block->size - sizeof(block_header_t);
    
    if (old_size >= size) {
        return ptr;  // Existing block is large enough
    }
    
    // Allocate new block
    void *new_ptr = malloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, old_size);
        free(ptr);
    }
    
    return new_ptr;
}

void *memset(void *s, int c, uint32_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return s;
}

void *memcpy(void *dest, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *memmove(void *dest, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    
    if (d < s) {
        // Copy forward
        while (n--) {
            *d++ = *s++;
        }
    } else {
        // Copy backward to handle overlap
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    
    return dest;
}

int memcmp(const void *s1, const void *s2, uint32_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    
    return 0;
}

uint32_t strlen(const char *s) {
    uint32_t len = 0;
    while (s[len]) {
        len++;
    }
    return len;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++)) {
        // Copy until null terminator
    }
    return dest;
}

char *strncpy(char *dest, const char *src, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n && src[i]; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const uint8_t *)s1 - *(const uint8_t *)s2;
}

int strncmp(const char *s1, const char *s2, uint32_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const uint8_t *)s1 - *(const uint8_t *)s2;
}

char *strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;  // Find end of dest
    while ((*d++ = *src++)) {
        // Copy src to end of dest
    }
    return dest;
}

memory_info_t memory_get_info(void) {
    if (!heap_initialized) memory_init();
    return mem_info;
}
