#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

// Memory allocation functions
void *malloc(uint32_t size);
void free(void *ptr);
void *calloc(uint32_t nmemb, uint32_t size);
void *realloc(void *ptr, uint32_t size);

// Memory manipulation functions
void *memset(void *s, int c, uint32_t n);
void *memcpy(void *dest, const void *src, uint32_t n);
void *memmove(void *dest, const void *src, uint32_t n);
int memcmp(const void *s1, const void *s2, uint32_t n);

// String functions
uint32_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, uint32_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, uint32_t n);
char *strcat(char *dest, const char *src);

// Memory info
typedef struct {
    uint32_t total;
    uint32_t used;
    uint32_t free;
    uint32_t heap_start;
    uint32_t heap_end;
} memory_info_t;

// Initialize memory subsystem
void memory_init(void);

// Get memory statistics
memory_info_t memory_get_info(void);

#endif // MEMORY_H
