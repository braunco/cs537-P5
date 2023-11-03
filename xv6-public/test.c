#include "types.h"
#include "stat.h"
#include "user.h"
#include "mmap.h"
int main(void)
{
    // Connor's test:
    int length = 12000; // one page
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
    printf(1, "yo\n");

    // charVers = (char*)newAddr;
    // charVers[0] = 'b';
    // charVers[1] = '\0';

    // printf(1, "val: %s\n", charVers);

    exit();
}