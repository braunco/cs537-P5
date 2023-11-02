#include "types.h"
#include "stat.h"
#include "user.h"
#include "mmap.h"
int main(void)
{
    /*
    //void *mmap(void *addr, int length, int prot, int flags, int fd, int offset);
    void *getback;
    void *bruh;
    bruh = (void*)0x60000000;
    getback = mmap(bruh, 2, 4, 6, 8, 10);

    if((int)getback < 0) {
        printf(1, "it errored\n");
    }

    exit();
    */

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


    
    // // Test writing to allocated memory
    // addr[0] = 'a';
    // addr[length - 1] = 'z';

    // if(addr[0] != 'a' || addr[length - 1] != 'z') {
    //     printf(1, "Memory write failed\n");
    // } else {
    //     printf(1, "Memeory write succeeded\n");
    // }

    exit();
}