#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <asm/unistd.h>
#include <linux/linkage.h>
#include <linux/syscalls.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include <asm/pgtable.h>

unsigned long** find_system_call_table();

/*
 * In case the symbol of the system call table is
 * exported and available in "/proc/kallsyms", we
 * get its address from it
 */

#ifdef CONFIG_KALLSYMS
unsigned long** find_system_call_table(){
        return kallsyms_lookup_name("sys_call_table");
}
#else
/*
 * If it's not exported, we have to scan the whole kernel
 * address space and we stop as we find an entry corresponding
 * to the system call "sys_close (one of the few that are exported)
 */
unsigned long** find_system_call_table(){
        unsigned long** sys_table;
        unsigned long offset = PAGE_OFFSET;
        while(offset < ULONG_MAX){
                sys_table = (unsigned long **)offset;
                if(sys_table[__NR_close] == (unsigned long *)sys_close)
                        return sys_table;
                offset += sizeof(void *);
        }
}
#endif

/*
 * Finds the page corresponding to the given address and
 * set its R/W attribute
 */

void make_page_writable(unsigned long address){

        /*
         * Get the address of the page
         */

        pte_t* page;
        int level=0;
        if(!(page = lookup_address(address, &level)))
                return -1;

        /*
         * Set writable
         */

        set_pte_atomic(page, pte_mkwrite(*page));
}

/*
 * Finds the page corresponding to the given address and
 * set its R/W attribute
 */

void make_page_readable(unsigned long address){

        /*
         * Get the address of the page
         */

        pte_t* page;
        int level=0;
        if(!(page = lookup_address(address, &level)))
                return -1;

        /*
         * Set writable
         */

        set_pte_atomic(page, pte_clear_flags(*page, _PAGE_RW));
}

/*
 * The definition of my custom system calls (kernel service routine)
 */

asmlinkage long sys_get_barrier(){
        printk(KERN_INFO "System call invoked");
        return 49;
}

/*
 * "Dynamically" add system calls to the system:
 * -> after having added four new entries in the
 * system call table and having set the corresponding
 * function to "sys_ni_call" (which is designed for
 * not implemented system calls), now we replace them
 * with our custom kernel service routines
 */

int init_module(void) {

        /*
         * First we have to find the address of the system call table
         */

        unsigned long** system_call_table=find_system_call_table();

        /*
         * At this point we have to set the last four entries of the
         * system call table with our custom kernel service routines.
         * We first acquire a lock on the page containing the table
         * in order to avoid race conditions
         */

        spinlock_t table_lock;
        spin_lock_init(&table_lock);

        /*
         * Lock acquired: now we have to temporarily change the attribute
         * of the page containing the system table in order to be allowed
         * to modify it
         */

        make_page_writable(system_call_table);

        /*
         * Set the first custom kernel service routine
         */

        system_call_table[338]=sys_get_barrier;

        /*
         * Restore original setting of the page containing the system table
         */

        make_page_readable(system_call_table);

        /*
         * Now we are done with system call table, so release the lock
         */

        spin_unlock(&table_lock);

        printk(KERN_INFO "Hello address:%lx\n",system_call_table);

        return 0;

}

void cleanup_module(void) {
        printk(KERN_INFO "Goodbye world 1.\n");

}

MODULE_LICENSE("GPL");