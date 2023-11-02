#include "types.h"
#include "stat.h"
#include "user.h"
#include "mmap.h"
int main(int argc, char *argv[])
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
    int length = 4096; // one page
    char *addr = mmap(0, length, PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    if((int)addr == -1) {
        printf(1, "mmap failed\n");
        exit();
    }

    // Test writing to allocated memory
    addr[0] = 'a';
    addr[length - 1] = 'z';

    if(addr[0] != 'a' || addr[length - 1] != 'z') {
        printf(1, "Memory write failed\n");
    } else {
        printf(1, "Memeory write succeeded\n");
    }

    exit();
}