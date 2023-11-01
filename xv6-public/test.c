#include "types.h"
#include "stat.h"
#include "user.h"
int main(void)
{
    printf(1, "This a test\n");

    //void *mmap(void *addr, int length, int prot, int flags, int fd, int offset);
    void *getback;
    void *bruh;
    bruh = (void*)10;
    getback = mmap(bruh, 1, 1, 1, 1, 1);

    printf(1, "should be 11: %d\n", (int)getback);

    exit();
}