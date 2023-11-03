#include "types.h"
#include "stat.h"
#include "user.h"
#include "mmap.h"
int main(void)
{

    /*
    // New test: Basic functionality of mmap
    int length1 = 4096; // one page
    void *addr1 = mmap((void*)0x60000000, length1, PROT_WRITE, MAP_ANON|MAP_FIXED, -1, 0);
    if((int)addr1 == -1) {
        printf(1, "mmap 1 failed\n");
        exit();
    }
    // Write to allocated mem
    char *test1 = (char*)addr1;
    test1[0] = 'a';
    test1[length1 - 1] = 'z';

    // Read back the values
    if (test1[0] != 'a' || test1[length1 - 1] != 'z') {
        printf(1, "memory write failed 1\n");
        exit();
    } else {
        printf(1, "Memory write succeeded 1\n");
    }
    */

    /*
    // New test: Multi-page allocation for mmap
    int length2 = 40960; // 10 pages
    void *addr2 = mmap((void*)0x60000000, length2, PROT_WRITE, MAP_ANON|MAP_FIXED, -1, 0);
    if((int)addr2 == -1) {
        printf(1, "mmap 2 failed\n");
    }
    char *test2 = (char*)addr2;

    // write to each allocated page
    for (int i = 0; i < 10; i++) {
        test2[i * 4096] = 'a' + i; //Write different characters at the start of each page
    }

    // Verify vals
    for (int i = 0; i < 10; i++) {
        if(test2[i * 4096] != 'a' + i) {
            printf(1, "Memory write failed at page%d\n", i);
            exit();
        }
        else {
            printf(1, "Memory write succeeded 2\n");
        }
    }
    */


    /*
    // New test: non-page aligned address failure for mmap
    int length3 = 4096; // one page
    void *addr3 = mmap((void*)0x60000001, length3, PROT_WRITE, MAP_ANON|MAP_FIXED, -1, 0);
    if((int)addr3 == -1) {
        printf(1, "mmap 4 failed\n");
        exit();
    }
    */

    /*
    // New test: Address Already mapped for mmap
    int length4 = 4096;
    void *addr4 = mmap((void*)0x60000000, length4, PROT_WRITE, MAP_ANON|MAP_FIXED, -1, 0);
    if ((int)addr4 == -1) {
        printf(1, "Original mapping failed");
        exit();
    }

    // Write to allocated mem
    char *test4 = (char*)addr4;
    test4[0] = 'a';
    test4[length4 - 1] = 'z';

    
    // This should fail because it already exists there
    void *addr4b = mmap((void*)0x60000000, length4, PROT_WRITE, MAP_ANON|MAP_FIXED, -1, 0);
    if ((int)addr4b == -1) {
        printf(1, "mmap 4 failed\n");
        printf(1, "This was expected. SUCCESS!\n");
    }

    // Read back the values after trying to get the same memory - just making sure it didn't affect what was already there
    if (test4[0] != 'a' || test4[length4 - 1] != 'z') {
        printf(1, "memory write failed 1\n");
        exit();
    } else {
        printf(1, "Memory write post try: succeeded 1\n");
    }
    */

    // New test: Basic allocation and deallocation for munmap
    int length5 = 4096;
    void *addr5 = mmap((void*)0x60000000, length5, PROT_WRITE, MAP_ANON|MAP_FIXED, -1, 0);
    if((int)addr5 == -1) {
        printf(1, "mmap 5 failed\n");
        exit();
    }
    
    // Write to allocated memory
    char *test5 = (char*)addr5;
    test5[0] = 'a';
    test5[length5 - 1] = 'z';

    // Dealloc the mem
    if (munmap(addr5, length5) == -1) {
        printf(1, "munmap failed\n");
        exit();
    }

    // Try to write to that location again now that it's gone
    int length5b = 4096;
    void *addr5b = mmap((void*)0x60000000, length5b, PROT_WRITE, MAP_ANON|MAP_FIXED, -1, 0);
    if((int)addr5b == -1) {
        printf(1, "mmap 5 failed\n");
        exit();
    }
    
    // Write to allocated memory
    char *test5b = (char*)addr5b;
    test5b[0] = 'b';
    test5b[length5b - 1] = 'y';
    
    // Read back the values after trying to get the same memory - just making sure it didn't affect what was already there
    if (test5b[0] != 'b' || test5b[length5b - 1] != 'y') {
        printf(1, "memory write failed 5b\n");
        exit();
    } else {
        printf(1, "Memory write post try: succeeded 5b\n");
    }

    printf(1, "Dealloc worked correctly.");
    

    /*
    // Connor's test:
    int length = 12000; // 3 pages
    void *addr = mmap((void*)0x60000000, length, PROT_WRITE, MAP_ANONYMOUS|MAP_FIXED, -1, 0);
    printf(1, "addr=%p", addr);
    if((int)addr == -1) {
        //printf(1, "addr=%p\n", addr);
        printf(1, "mmap failed\n");
        exit();
    }

    char* charVers = (char*)addr;

    charVers[0] = 'a';
    charVers[1] = '\0';

    printf(1, "val: %s\n", charVers);

    munmap(addr, length);
    printf(1, "munmap didn't crash\n");

    //printf(1, "val: %s\n", charVers);

    void* newAddr = mmap((void*)0x60000000, length, PROT_WRITE, MAP_ANONYMOUS|MAP_FIXED, -1, 0);
    if((int)newAddr < 0)
    {
        printf(1, "task failed successfully\n");
        //printf(1, "val: %s\n", charVers);
    }

    // charVers = (char*)newAddr;
    // charVers[0] = 'b';
    // charVers[1] = '\0';

    // printf(1, "val: %s\n", charVers);
    */

    exit();
}