#include <stdint.h>
void *entry(void){ __asm__ volatile("udf #0x42"); return 0; }
