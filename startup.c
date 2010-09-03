
#include <l4/sys/types.h>

/* Also put thread data at the end of the address space */
const l4_addr_t l4thread_stack_area_addr = 0x9a000000;
const l4_addr_t	l4thread_tcb_table_addr  = 0x99000000;

/* Set max amount of threads which can be created, weak symbol from
 * thread library */
const int l4thread_max_threads = 128;
/* Max amount of stack size */
const int l4thread_max_stack   = 0x10000;
