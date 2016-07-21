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
#include "barrier.h"
#include <linux/ipc.h>
#include <linux/idr.h>
#include <linux/gfp.h>
#include <linux/rwsem.h>
#include <linux/sem.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/ipc_namespace.h>
#include <linux/ipc.h>
#include <asm-generic/current.h>

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
 * Function to create a new instance of a barrier, given the
 * namespace a few parameters: flags and key
 * Returns the IPC identifier of the newly created barrier or
 * some error code in case something goes wrong
 */

int newbarrier(struct ipc_params * params){

        /*
         * The unique IPC identifier of the new barrier; recall that
         * this ID is unique within a "data structure type scope",i.e.
         * there may be an IPC message queue or IPC semaphore with
         * the same id as this barrier we are creating
         */

        int id;

        /*
         * The return value of the function
         */

        int retval;

        /*
         * The structure representing the barrier we are creating
         */

        struct barrier_struct *barrier;

        /*
         * The key requested by the user process
         */

        key_t key = params->key;

        /*
         * The flags specified when the barrier was
         * requested
         */

        int barrierflags = params->flg;

        /*
         * Allocate the barrier
         */

        barrier = kmalloc(sizeof (*barrier),GFP_KERNEL);

        /*
         * Return error in case there's not enough memory
         */

        if (!barrier) {
                return -ENOMEM;
                }

        /*
         * Initialize dynamically allocated memory
         */

        memset (barrier, 0, sizeof (*barrier));

        /*
         * Set the key and mode field of perm kern struct
         */

        barrier->barrier_perm.mode=barrierflags;
        barrier->barrier_perm.key = key;

        /*
         * We don't care about security
         */

        barrier->barrier_perm.security = NULL;

        /*
         * Set the IPC identifier (id) for the new barrier
         */

        id = ipc_addid(&sem_ids(ns), &sma->sem_perm, ns->sc_semmni);
        if (id < 0) {
                security_sem_free(sma);
                ipc_rcu_putref(sma);
                return id;
                }
        ns->used_sems += nsems;
        sma->sem_base = (struct sem *) &sma[1];
        for (i = 0; i < nsems; i++)
                INIT_LIST_HEAD(&sma->sem_base[i].sem_pending);
        sma->complex_count = 0;
        INIT_LIST_HEAD(&sma->sem_pending);
        INIT_LIST_HEAD(&sma->list_id);
        sma->sem_nsems = nsems;
        sma->sem_ctime = get_seconds();
        sem_unlock(sma);
        return sma->sem_perm.id;
}

/**
 *	ipc_findkey	-	find a key in an ipc identifier set
 *	@ids: Identifier set
 *	@key: The key to find
 *
 *	Requires ipc_ids.rw_mutex locked.
 *	Returns the LOCKED pointer to the ipc structure if found or NULL
 *	if not.
 *	If key is found ipc points to the owning ipc structure
 *	NOTE: this function is defined as static in the source code, so
 *	we have to redifine it
 */

static struct kern_ipc_perm *ipc_findkey(struct ipc_ids *ids, key_t key)
{
        struct kern_ipc_perm *ipc;
        int next_id;
        int total;

        /*
         * Search through all the IDs corresponding to instantiated barriers
         * (ipc!=NULL), the one whose key corresponds to the key provided as
         * parameter
         */

        for (total = 0, next_id = 0; total < ids->in_use; next_id++) {
                ipc = idr_find(&ids->ipcs_idr, next_id);

                if (ipc == NULL)
                        continue;

                if (ipc->key != key) {
                        total++;
                        continue;
                }

                /*
                 * Acquire a lock over the newly allocated structure
                 */

                spin_lock(&ipc->lock);
                return ipc;
        }

        /*
         * If none of the instantiated barriers matches the given key,
         * return NULL
         */

        return NULL;
}

/*
 * Function called by the service kernel routine in order to create a
 * new kern_perm_struct, in case no structure corresponding to the given
 * key is found; otherwise return the id of the structure corresponding
 * to the given key. The IPC subsystem allows to create instances of the
 * other IPC structures with a "IPC_PRIVATE" key: this ensures that a new
 * instance will be created, with a new key (and identifier, of course)
 * but we are not required to implement this feature
 */

int barrier_get(struct ipc_ids* ids, struct ipc_ops* ops, struct ipc_params* params){

        /*
         * Instantiate new "kern_ipc_perm" structure: this is used to handle permissions
         * and sequence number of the barrier
         */

        struct kern_ipc_perm* ipcp;

        /*
         * Get flags passed
         */

        int flags=params->flg;

        /*
         * The code used as result of the system call invokation
         */

        int err=EAGAIN;

        while(err==EAGAIN){

                /*
                 * Call to idr subsystem before the new ID is returned;
                 * (compliant to idr API) it prepares memory to store
                 * it
                 */

                err=idr_pre_get(&ids->ipcs_idr,GFP_KERNEL);

                if(!err)

                        /*
                         * Return out of memory
                         */

                        return ENOMEM;

                /*
                 * Acquire exclusive lock on ids before modifying it
                 * (another process may be trying to get a barrier
                 * at the same time)
                 */

                down_write(&ids->rw_mutex);

                /*
                 * Look for the kern ipc perm struct associated to
                 * the barrier identified by given key
                 */

                ipcp = ipc_findkey(ids, params->key);

                /*
                 * If no kern_ipc_perm struct with the given key is
                 * found, then behave depending on the provided API,
                 * otherwise
                 */

                if (ipcp == NULL) {

                        /*
                         * Fail because no kern_perm_create structure
                         * with the given key was found and the flag
                         * BARRIER_CREATE is not set (so the requestor
                         * did not mean to create one, but rather one
                         * corresponding to the given key)
                         */

                        if (!(flags & BARRIER_CREATE))
                                err = -ENOENT;

                        /*
                         * If idr_pre_get returns zero,
                         * it means something went wrong
                         * so fail
                         */

                        else if (!err)
                                err = -ENOMEM;

                        /*
                         * If idr_pre_get returns a positive
                         * number (no error occurred) and the
                         * BARRIER_CREATE is given, create a
                         * new barrier
                         */

                        else
                                //err = ops->getnew(ns, params);
                                err=newbarrier(params);
                } else {

                        /*
                         * Found the barrier corresponding to the
                         * given key; the object has been locked.
                         * If both the flag BARRIER_CREATE and the flag
                         * BARRIER_EXCL were specified, fail, otherwise
                         * return the unique id of the structure
                         */

                        if (flags & BARRIER_CREATE && flags & BARRIER_EXCL)
                                err = -EEXIST;
                        else {
                                err = &ipcp->id;

                                /*
                                 * Perform no more checks, unlike other ipc
                                 * structures
                                 * if (ops->more_checks)
                                        err = ops->more_checks(ipcp, params);
                                        */
                                /*
                                 * Perform no security check
                                if (!err)

                                        err = ipc_check_perms(ipcp, ops, params);
                                        */

                        }

                        /*
                         * Release locks on kern_perm_struct
                         */

                        spin_unlock(&ipcp->lock);
                }


                /*
                 * Release lock
                 */

                up_write(&ids->rw_mutex);
        }

        return err;
}

/*
 * The definition of my custom system calls (kernel service routine).
 * Parameters are:
 * key_t key: this value is given in order to ask for a certain barrier
 * flags: flags that determine how the barrier should be created or returned,
 * if it exists
 * Returns:
 * the IPC id arbitrary assigned to the barrier, if created, or the of
 * the barrier identified by the key, if already existing
 */

asmlinkage long sys_get_barrier(key_t key,int flags){
        printk(KERN_INFO "System call invoked");

        /*
         * The representation of the IPC object within the IPC subsystem
         */

        struct ipc_ops barrier_ops;
        struct ipc_params barrier_params;

        /*
         * Set operations available for this new synchronization object
         */

        barrier_ops.getnew = newbarrier;
        barrier_ops.associate = NULL;
        barrier_ops.more_checks = NULL;

        /*
         * Set the structure representing parameters to be used to create
         * the new synchronization object
         */

        barrier_params.key = key;
        barrier_params.flg = flags;

        /*
         * The ipcget function is in charge of create a new barrier according
         * to the flags provided or find the one corresponding to the provided
         * key
         */

        return barrier_get(&barrier_ids, &barrier_ops, &barrier_params);


}

/*
 * BARRIER INITIALIZATION:
 * First step in the initialization of the the barrier data structure:
 * initialize the "ipc_ids" identifiers set data structure: this is
 * in charge of handling the ids assigned to every instance of this
 * data structure. It makes use of the "idr API".
 * There's one per data structure and the namespace of each process
 * keeps a reference to them => since IPC does not include our barrier,
 * we store it somewhere else and then export the location in order
 * for all the processes to find it when they have to deal with barriers.
 * (this function resembles "ipc_init_ids" function)
 * SEQ_MULTIPLIER is the maximum number of barriers allowed:32768
 * The id of a resource is calculated (as in IPC subsystem) as follows:
 *
 * id=s*SEQ_MULTIPLIER+i
 *
 * where "s" is a "slot sequence number" relative to the resource type (barrier
 * in this case) and i is the "slot index" for the specific instance among all
 * the instantiated barriers, arbitrary assigned.
 * "s" is initializes to 0 and incremented at every resource allocation
 */

void barrier_init(struct ipc_ids* barrier_ids){
        ipc_init_ids(barrier_ids);
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

        /*
         * Initialize data structures necessary to handle a barrier
         */

        barrier_init(&barrier_ids);

        return 0;

}

void cleanup_module(void) {
        printk(KERN_INFO "Goodbye world 1.\n");

}

MODULE_LICENSE("GPL");