#include "types.h"
#include "stat.h"
#include "user.h"
int main(void)
{
    //void *mmap(void *addr, int length, int prot, int flags, int fd, int offset);
    void *getback;
    void *bruh;
    bruh = (void*)0x60000000;
    getback = mmap(bruh, 2, 4, 6, 8, 10);

    if((int)getback < 0) {
        printf(1, "it errored\n");
    }

    exit();
}