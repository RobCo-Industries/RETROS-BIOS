#!/usr/bin/env python3
"""
Unit tests for RETROS-BIOS memory functions
Tests memory.c functions in a host environment
"""

import subprocess
import tempfile
import os

def create_test_program(test_code):
    """Create a test program that can be compiled and run on host"""
    program = f"""
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

// Mock implementations for testing
char __bss_end[0];

// Memory functions from memory.c (simplified for testing)
void *memset(void *s, int c, uint32_t n) {{
    uint8_t *p = (uint8_t *)s;
    while (n--) {{
        *p++ = (uint8_t)c;
    }}
    return s;
}}

void *memcpy(void *dest, const void *src, uint32_t n) {{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {{
        *d++ = *s++;
    }}
    return dest;
}}

int memcmp(const void *s1, const void *s2, uint32_t n) {{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    
    while (n--) {{
        if (*p1 != *p2) {{
            return *p1 - *p2;
        }}
        p1++;
        p2++;
    }}
    
    return 0;
}}

uint32_t strlen(const char *s) {{
    uint32_t len = 0;
    while (s[len]) {{
        len++;
    }}
    return len;
}}

int strcmp(const char *s1, const char *s2) {{
    while (*s1 && (*s1 == *s2)) {{
        s1++;
        s2++;
    }}
    return *(const uint8_t *)s1 - *(const uint8_t *)s2;
}}

char *strcpy(char *dest, const char *src) {{
    char *d = dest;
    while ((*d++ = *src++)) {{
        // Copy until null terminator
    }}
    return dest;
}}

int main() {{
{test_code}
    printf("All tests passed!\\n");
    return 0;
}}
"""
    return program

def run_test(test_name, test_code):
    """Compile and run a test"""
    print(f"Running {test_name}...", end=" ")
    
    with tempfile.NamedTemporaryFile(mode='w', suffix='.c', delete=False) as f:
        f.write(create_test_program(test_code))
        source_file = f.name
    
    try:
        # Compile
        output_file = source_file.replace('.c', '')
        result = subprocess.run(
            ['gcc', '-o', output_file, source_file],
            capture_output=True,
            text=True
        )
        
        if result.returncode != 0:
            print("FAIL (compilation)")
            print(result.stderr)
            return False
        
        # Run
        result = subprocess.run([output_file], capture_output=True, text=True)
        
        if result.returncode != 0:
            print("FAIL (runtime)")
            print(result.stderr)
            return False
        
        print("PASS")
        return True
    
    finally:
        # Cleanup
        try:
            os.unlink(source_file)
            os.unlink(output_file)
        except:
            pass

def main():
    print("=" * 50)
    print("RETROS-BIOS Memory Function Tests")
    print("=" * 50)
    print()
    
    tests_passed = 0
    tests_failed = 0
    
    # Test 1: memset
    if run_test("memset", """
    uint8_t buffer[10];
    memset(buffer, 0xAA, 10);
    for (int i = 0; i < 10; i++) {
        assert(buffer[i] == 0xAA);
    }
"""):
        tests_passed += 1
    else:
        tests_failed += 1
    
    # Test 2: memcpy
    if run_test("memcpy", """
    uint8_t src[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    uint8_t dst[10];
    memcpy(dst, src, 10);
    for (int i = 0; i < 10; i++) {
        assert(dst[i] == src[i]);
    }
"""):
        tests_passed += 1
    else:
        tests_failed += 1
    
    # Test 3: memcmp equal
    if run_test("memcmp (equal)", """
    uint8_t buf1[5] = {1, 2, 3, 4, 5};
    uint8_t buf2[5] = {1, 2, 3, 4, 5};
    assert(memcmp(buf1, buf2, 5) == 0);
"""):
        tests_passed += 1
    else:
        tests_failed += 1
    
    # Test 4: memcmp different
    if run_test("memcmp (different)", """
    uint8_t buf1[5] = {1, 2, 3, 4, 5};
    uint8_t buf2[5] = {1, 2, 4, 4, 5};
    assert(memcmp(buf1, buf2, 5) != 0);
"""):
        tests_passed += 1
    else:
        tests_failed += 1
    
    # Test 5: strlen
    if run_test("strlen", """
    assert(strlen("") == 0);
    assert(strlen("hello") == 5);
    assert(strlen("test string") == 11);
"""):
        tests_passed += 1
    else:
        tests_failed += 1
    
    # Test 6: strcmp equal
    if run_test("strcmp (equal)", """
    assert(strcmp("hello", "hello") == 0);
    assert(strcmp("", "") == 0);
"""):
        tests_passed += 1
    else:
        tests_failed += 1
    
    # Test 7: strcmp different
    if run_test("strcmp (different)", """
    assert(strcmp("hello", "world") != 0);
    assert(strcmp("abc", "abd") < 0);
    assert(strcmp("abd", "abc") > 0);
"""):
        tests_passed += 1
    else:
        tests_failed += 1
    
    # Test 8: strcpy
    if run_test("strcpy", """
    char src[] = "Hello, World!";
    char dst[20];
    strcpy(dst, src);
    assert(strcmp(dst, src) == 0);
"""):
        tests_passed += 1
    else:
        tests_failed += 1
    
    # Summary
    print()
    print("=" * 50)
    print(f"Passed: {tests_passed}")
    print(f"Failed: {tests_failed}")
    print("=" * 50)
    
    return 0 if tests_failed == 0 else 1

if __name__ == "__main__":
    exit(main())
