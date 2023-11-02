/* Define mmap flags */
#define MAP_PRIVATE 0x0001
#define MAP_SHARED 0x0002
#define MAP_ANONYMOUS 0x0004
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FIXED 0X0008
#define MAP_GROWSUP 0X0010

/* Protections on memory mapping */
#define PROT_READ 0x1
#define PROT_WRITE 0X2


#define PAGE_SIZE 4096