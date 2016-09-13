#ifndef BARRIERSYNCHRONIZATION_HELPER_C_H
#define BARRIERSYNCHRONIZATION_HELPER_C_H

#define WP_X86 0x00010000

/*
 * Address of "sys_ ni_syscall", the system call corresponding
 * to free entries of the system call table
 * THIS HAS TO BE TAKEN FROM THE SYSTEM MAP BECAUSE THE SYMBOL
 * IS NOT EXPORTED
 */

unsigned long not_implemented_syscall=3222697504;

/*
 * Address of the system call table
 */

unsigned long* system_call_table;

/*
 * Indexes of the system calls table entries modified by the module
 */

unsigned int restore[4];

void make_page_writable(pte_t* page);

/*
 * Find address of the system call table
 */

unsigned long* find_system_call_table(void);

void find_free_syscalls(unsigned long* table, unsigned int* restore);

#endif //BARRIERSYNCHRONIZATION_HELPER_C_H
