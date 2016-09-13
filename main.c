#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <asm/unistd.h>
#include <linux/linkage.h>
#include <linux/syscalls.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include "barrier.h"
#include "helper.h"
#include <linux/ipc.h>
#include <linux/idr.h>
#include <linux/gfp.h>
#include <linux/rwsem.h>
#include <linux/sem.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/ipc_namespace.h>
#include <asm-generic/current.h>
#include <asm-generic/pgtable.h>

void enable_write_protected_mode(unsigned long* cr0);
void disable_write_protected_mode(unsigned long* cr0);

/*
 * Pointer to the ipc_ids data structure used to keep track of  all the instances
 * of the barrier on the basis of their ids
 */

struct ipc_ids* barrier_ids;

/*
 * Lock the "barrier_ipc_perm" structure within an instance of
 * barrier
 *
 * @barrier pointer to the barrier whose barrier_ipc_perm has to
 * be blocked
 */

void barrier_lock(struct barrier_struct* barrier){
        struct barrier_ipc_perm* barrier_ipc_perm=&(barrier->barrier_perm);
        spin_lock(&(barrier_ipc_perm->lock));
}

/*
 * Unlock the "barrier_ipc_perm" structure within an instance of
 * barrier
 *
 * @barrier: pointer to the barrier whose barrier_ipc_perm has to
 * be blocked
 */

void barrier_unlock(struct barrier_struct* barrier){
        struct barrier_ipc_perm* barrier_ipc_perm=&(barrier->barrier_perm);
        spin_unlock(&(barrier_ipc_perm->lock));
}

/*
 * Custom version of the function "ipc_addid" of the IPC subsystem.
 * Just like the latter, add a new entry (namely allocate a new ID)
 * in the structure that manages the IDs for the barrier (idr mapping)
 * and return it. The newly created entry is returned in a locked
 * state; also this function is invoked holding the lock on the
 * ipc_ids structure.
 *
 * @ids: pointer to the "ipc_ids" structure that manages ids of barriers
 * @new: pointer to the newly created barrier that has to be assigned a
 *       new ID
 *
 * Returns a unique identifier for the new instance of barrier
 * This is calculated (as in IPC subsystem) as follows:
 *
 * id=s*SEQ_MULTIPLIER+i
 *
 * where "s" is a "slot sequence number" relative to the barrier and i is the
 * id returned by the IDR API
 * SEQ_MULTIPLIER is the maximum number of barriers allowed:32768
 */

int barrier_addid(struct ipc_ids *ids, struct barrier_ipc_perm *new){

        /*
         * Final ID and error code to be returned (either the former or
         * the latter)
         */

        int id, err,seq;

        /*
         * Check whether the maximum number of allocated IDs has already
         * been reached: if so return the code error -ENOSPC (no space left)
         */

        if(ids->in_use>=BARRIER_IDS_MAX)
                return -ENOSPC;

        /*
         * Initialize a lock on the barrier_ipc_perm in order to
         * synchronize access to the idr data structure
         */

        spin_lock_init(&new->lock);

        /*
         * Acquire the lock on the barrier_ipc_perm structure before assigning it
         * a new ID: access to IDR API has to be serialized
         */

        spin_lock(&new->lock);

        /*
         * Get next available ID for the new instance of barrier using the
         * idr subsystem; the third parameter is initialized to the new ID
         * assigned.
         *
         * The function returns -ENOSPC in case of error, 0 otherwise
         */

        err = idr_get_new(&ids->ipcs_idr, new, &id);

        /*
         * In case of error, unlock the barrier and return the error code
         */

        if(err){
                spin_unlock(&new->lock);
                return err;
        }

        /*
         * Increment the counter of ids in use by one
         */

        ++(ids->in_use);

        /*
         * Get the actual sequence number of barriers: if it's bigger the
         * maximum sequence number, reset it to 0
         */

        seq=ids->seq;
        if(seq>ids->seq_max)
                ids->seq=0;

        /*
         * Assign the ID to the barrier
         */

        new->id=ids->seq*BARRIER_IDS_MAX+id;

        return  id;

}

/*
 * Function to get a new instance of a barrier
 *
 * @params: flags and key associated to the new barrier
 *
 * Returns the IPC identifier of the newly created barrier or
 * some error code in case something goes wrong.
 *
 * This function is called while holding the r/w semaphore of
 * the global (shared among all barriers) ipc_ids structure
 * as a writer
 *
 */

int newbarrier(struct barrier_params * params){

        /*
         * The unique IPC identifier of the new barrier; recall that
         * this ID is unique within a "data structure type scope",i.e.
         * there may be an IPC message queue or IPC semaphore with
         * the same id as this barrier we are creating
         */

        int id,i;

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
         * Allocate the memory for the barrier
         */

        barrier = (struct barrier_struct *)kmalloc(sizeof (*barrier),GFP_KERNEL);

        /*
         * Return error in case there's not enough memory left
         * for the barrier
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
         * Get a new id for the new instance of barrier from the
         * structure "barrier_ids"
         * In case a free ID is found, the barrier is locked and
         * the ID is returned.
         */

        id = barrier_addid(&barrier_ids, &barrier->barrier_perm);

        /*
         * If the ID is set to -1, it means something went wrong with
         * the allocation of the ID for the new barrier: since the ID
         * is mandatory for the instance of barrier, we free the instance
         * and return the ID as error code
         */

        if(id==-1){
                kfree(barrier);
                return id;
        }

        /*
         * Initialize all the list of pointers to the wait queues of processes,
         * one per SYNCHRONIZATION TAG
         */

        for(i=0;i<BARRIER_TAGS;i++){
                INIT_LIST_HEAD(&(barrier->queues[i]));
        }

        /*
         * Unlock the new instance of barrier
         */

        barrier_unlock(barrier);

        return id;
}

/*
 * Find the barrier_ipc_perm structure of the instance of barrier
 * corresponding to the given key
 *
 * @ids: the "ipc_ids" that tracks the IDs of all the barriers
 *       instantiated thanks to the field "ipcs_idr"
 * @key: the key to be searched for
 *
 * This function has to be invoked holding the semaphore of the
 * ids structure.
 * Returns the barrier_ipc_perm structure corresponding to the given
 * key (locked) or NULL in case it is not found
 */

struct barrier_ipc_perm *find_barrier_key(struct ipc_ids *ids, key_t key)
{
        struct barrier_ipc_perm *ipc;
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
                 * Acquire a lock over the found barrier_ipc_perm structure
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
 * Create a barrier when the given key is not BARRIER_PRIVATE: other processes
 * will be available to use the same synchronization object
 *
 * @ids: the ipc_ids structure used to keep track of all the instances of
 *       barrier and assign IDs to them
 * @params: key and flags to be used to create the new instance of barrier
 *
 * Returns the ID assigned to the newly created barrier or -1 in case of error
 */

int barrier_get_public(struct ipc_ids* ids, struct barrier_params* params){

        int err=-EAGAIN;

        /*
         * The barrier_ipc_perm of the barrier instance corresponding to the
         * given key
         */

        struct barrier_ipc_perm* barrier_perm;

        /*
         * Flag specified as parameter to the system call
         */

        int flg=params->flg;

        /*
         * Try to create a new barrier until successful
         */

        while(err==-EAGAIN) {

                /*
                 * We have to check if there's enough space for another entry in the idr object in
                 * case an instance corresponding to the given key is not found but the flag
                 * BARRIER_CREATE has been specified as parameter to the system call
                 * (see same function call in "barrier_get_private")
                 */

                err = idr_pre_get(&ids->ipcs_idr, GFP_KERNEL);

                /*
                 * (see same function call in "barrier_get_private")
                 */

                down_write(&ids->rw_mutex);

                /*
                 * Find the barrier_ipc_perm corresponding to the given key
                 */

                barrier_perm=find_barrier_key(ids,params->key);

                /*
                 * In case the barrier_ipc_perm is not found, check the given flags
                 */

                if(!barrier_perm){

                        /*
                         * If the BARRIER_CREATE flag was not specified, it means
                         * no barrier has to be created in case the key is not found,
                         * so exit with error -ENOENT
                         */

                        if(!(flg & BARRIER_CREATE))
                                err -ENOENT;

                                /*
                                 * If the BARRIER_CREATE flag was specified (hence a new barrier
                                 * with the given key has to be created) but there's no space for
                                 * a new entry in the IDR object, exit with error -ENOMEM
                                 */

                        else if(!err)
                                err=-ENOMEM;
                        else
                                /*
                                 * The BARRIER_CREATE was specified, so a new barrier has
                                 * to be created and also there's space for it
                                 */
                                err=newbarrier(params);
                }

                /*
                 * A barrier with the given key does exists
                 */

                else{
                        /*
                         * In case the process that issued the system call wants to create
                         * a barrier with the given key exclusively (BARRIER_CREATE and BARRIER_EXCL
                         * flags specified), exit with error -EEXIST;
                         */

                        if(flg&BARRIER_CREATE && flg&BARRIER_EXCL)
                                err=-EEXIST;
                        /*
                         * If none or only the flag is specified, return the ID of the barrier
                         * corresponding to the given key
                         */

                        else
                                err=barrier_perm->id;

                        /*
                         * Always unlock the barrier_ipc_perm structure found before returning
                         */

                        spin_unlock(&(barrier_perm->lock));
                }

                /*
                 * Release write semaphore: if the associated wait queue is not
                 * empty, a new process becomes runnable and is allowed to access
                 * the ids structure
                 */

                up_write(&ids->rw_mutex);
        }

        /*
         * Return the code corresponding to the outcome of the
         * function
         */

        return  err;
}

/*
 * Create a barrier when the given key is BARRIER_PRIVATE: the barrier won't be
 * find by key, but only with its unique identifier
 *
 * @ids: the ipc_ids structure used to keep track of all the instances of
 *       barrier and assign IDs to them
 * @params: key and flags to be used to create the new instance of barrier
 *
 * Returns the ID assigned to the newly created barrier or -1 in case of error
 */

int barrier_get_private(struct ipc_ids* ids, struct barrier_params* params){

        int err=-EAGAIN;

        /*
         * Try to create a new barrier until successful
         */

        while(err==-EAGAIN) {

                /*
                 * This is part of the "idr" facility, used to manage IDs of the barrier:
                 * here we allocate memory for the worst possible case of IDs allocation.
                 * It returns 0 in case the system is out of memory, 1 otherwise.
                 * This first phase in the ID allocation using the idr mapping does not require
                 * to acquire any lock: we are just reserving memory which is globally accessible.
                 * The flag GFP_KERNEL tells the kernel where to find the new memory to be allocated
                 * for the new entries in the idr mappings
                 */

                err = idr_pre_get(&ids->ipcs_idr, GFP_KERNEL);

                /*
                 * If allocation fails because of memory shortage,return -ENOMEM (the minus sign
                 * is needed in order to adhere the linux kernel convention of returning negative
                 * values in case of error)
                 */

                if (!err)
                        return -ENOMEM;

                /*
                 * Try to get exclusive write access on the ids structure in
                 * order to add a new id => the lock is necessary because
                 * another process may try to get another instance of a
                 * barrier concurrently, so we have to provide synchronized
                 * access to the only ipc_ids structure associated to our
                 * barrier.
                 * With semaphores, if a new process comes to access the ids
                 * and finds it locked, it is put in a wait queue until the
                 * resource is available, so it doesn't waste CPU cycles.
                 */

                down_write(&ids->rw_mutex);

                /*
                 * Create new instance of barrier: "err" is initialized to the
                 * IPC identifier of the new instance or to an error code if
                 * something goes wrong
                 */

                err = newbarrier(params);

                /*
                 * Release write semaphore: if the associated wait queue is not
                 * empty, a new process becomes runnable and is allowed to access
                 * the ids structure
                 */

                up_write(&ids->rw_mutex);
        }

        /*
         * Return the code corresponding to the outcome of the
         * function newbarrier
         */

        return  err;
}

/*
 * Function called by the service kernel routine in order to get the instance
 * of a barrier corresponding to the given key.
 * The interpretation of specified parameters resembles the one of the IPC subsystem.
 * In particular, if no barrier corresponding to the requested key is found and the
 * flag BARRIER_CREATE is specified, a new instance is created; if no barrier corresponding
 * to the given key is found and the BARRIER_CREATE flag was not specified, the function
 * returns the error message ENOENT (not existing).
 * A process can guarantee that it is the only one creating a barrier with the given
 * key with the BARRIER_EXCL flag: in fact, if the the flag is given but another barrier
 * with the same key is found, the function fails with error EEXIST
 * Finally, the key BARRIER_PRIVATE may be specified: in this case the barrier won't be visible
 * to other processes unless the process that created it gives them the unique ID of the
 * barrier
 *
 * @ids: the ipc_ids structure used to keep track of all the instances of
 *       barrier and assign IDs to them
 * @params: key and flags to be used to create the new instance of barrier
 */

int barrier_get(struct ipc_ids* ids, struct barrier_params* params){

        int flags,err;
        struct barrier_ipc_perm* barrierperm;

        if(params->key==BARRIER_PRIVATE)

                /*
                 * If the key was specified as BARRIER_PRIVATE, create a new the barrier assigning it
                 * the next available ID
                 */

                return  barrier_get_private(ids,params);
        else
                /*
                 * Find the particular instance of barrier corresponding to the given key
                 */

                return barrier_get_public(ids,params);
}

/*
 * SYSTEM CALL KERNEL SERVICE ROUTINES - start
 *
 * 1 - sys_get_barrier
 * 2 - sys_release_barrier
 * 3 - sys_sleep_on_barrier
 * 4 - sys_awake_barrier
 */

/*
 * Instantiate a new barrier synchronization object
 *
 * @ key: this value is given in order to ask for a certain barrier
 * @flags: flags that determine how the barrier should be created or returned,
 * if it exists (see "barrier_get" function)
 *
 * Returns:
 * the IPC id arbitrary assigned to the barrier, if created, or the of
 * the barrier identified by the key, if already existing
 */

asmlinkage long sys_get_barrier(key_t key,int flags){

        /*
         * Return value of this system call
         */

        int ret;

        /*
         * Create new structures to hold parameters
         */

        struct barrier_params barrier_params;

        printk(KERN_INFO "System call sys_get_barrier invoked with params: key=%d flags=%d\n",key,flags);

        /*
         * Set corresponding parameters to value received from wrapper
         * function
         */

        barrier_params.key = key;
        barrier_params.flg = flags;

        /*
         * Create a new barrier according to the flags provided or find
         * the one corresponding to the provided key
         * The return value from the below function coincides with the
         * return value of this system call
         */

        ret=barrier_get(&barrier_ids, &barrier_params);

        printk(KERN_INFO "System call sys_get_barrier returned this value:%d\n",ret);


}

/*
 * SYSTEM CALL KERNEL SERVICE ROUTINES - end
 */

/*
 * BARRIER INITIALIZATION:
 * First step in the initialization of the the barrier data structure:
 *
 * @barrier_ids :the "ipc_ids" identifiers set data structure. It is
 * in charge of handling the ids assigned to every instance of this
 * data structure.
 *
 * There's one per data structure and the namespace of each process
 * keeps a reference to them => since IPC does not include our barrier,
 * we store it somewhere else and then export the location in order
 * for all the processes to find it when they have to deal with barriers.
 * (this function resembles "ipc_init_ids" function)
 * The id of a resource is calculated (as in IPC subsystem) as follows:
 *
 * id=s*SEQ_MULTIPLIER+i
 *
 * where "s" is a "slot sequence number" relative to the resource type (barrier
 * in this case) and i is the "slot index" for the specific instance among all
 * the instantiated barriers, arbitrary assigned.
 * "s" is initializes to 0 and incremented at every resource allocation
 * SEQ_MULTIPLIER is the maximum number of barriers allowed:32768
 */

void barrier_init(struct ipc_ids* barrier_ids){

        /*
         * Initialize the read/write semaphore that protects
         * the ipc_ids structure for our barriers: the "count"
         * field of the semaphore is set to 0, the "wait_lock"
         * is set to "unlocked" and the list of waiting processes
         * ("wait_list") is set to an empty list.
         * We need to synchronize access to this structure because
         * it will be accessed concurrently by all the processes that
         * want to create a new barrier object
         */

        init_rwsem(&barrier_ids->rw_mutex);

        /*
         * Initially no IDs are allocated
         */

        barrier_ids->in_use = 0;

        /*
         * Same holds for sequence numbers
         */

        barrier_ids->seq=0;

        /*
         * Set the maximum for the sequence number of a barrier
         */

        barrier_ids->seq_max = INT_MAX/BARRIER_IDS_MAX;

        /*
         * Finally initialize the idr handle that will be used
         * to assign unique IDs to all the instances of barrier:
         * The type of this structure is "struct idr" and the
         * function "idr_init" simply allocates memory dynamically
         * to store it and also initializes its lock
         */

        idr_init(&barrier_ids->ipcs_idr);
}

/*
 * INSERT/REMOVE MODULE - start
 */

/*
 * INSERT MODULE
 * "Dynamically" add system calls to the system:
 * -> replace not implemented system calls (sys_ni_syscall) with custom system calls
 */

int init_module(void) {

        /*
         * Value stored in the register CR0
         */

        unsigned long cr0;

        /*
         * Find the address of the system call table
         */

        system_call_table=find_system_call_table();

        /*
         * Get the indexes of the free entries in the system call table,
         * i.e. those initialized to the address of "sys_ni_syscall".
         * This is necessary in order to restore the system call table
         * when the module is removed
         */

        find_free_syscalls(system_call_table,restore);

        /*
         * In case the CPU has WRITE-PROTECTED MODE enabled, even kernel
         * thread can't modify read-only pages, such those containing the
         * system call table => we first have to disable the WRITE-PROTECTED MODE
         * by clearing the corresponding bit (number 16) in the CR0 register.
         */

        disable_write_protected_mode(&cr0);

        /*
         * Replace system call "sys_ni_syscall" in the system call table
         * with our custom system calls: we assume that no other process
         * running on any other CPU ever tries to access the system call
         * not implemented => if this assumption holds, the following code
         * is thread-safe
         */

        //system_call_table[restore[0]]=(unsigned long)sys_get_barrier;
        //system_call_table[restore[1]]=not_implemented_syscall;
        //system_call_table[restore[2]]=not_implemented_syscall;
        //system_call_table[restore[3]]=not_implemented_syscall;

        /*
         * Restore original value of register CR0
         */

        enable_write_protected_mode(&cr0);

        /*
         * Allocate a new structure "ipc_ids" in order to keep track of
         * all the existing barrier objects as they are created
         */

        barrier_ids=kmalloc(sizeof(struct ipc_ids), GFP_KERNEL);

        /*
         * Initialize the newly created "ipc_ids" structure
         */

        barrier_init(barrier_ids);

        printk(KERN_INFO "Address of the ipc_ids:%lu\n",barrier_ids);

        /*
         * Log message about our just inserted module
         */

        printk(KERN_INFO "Module \"barrier_module\" inserted: index of replaced system calls:%d,%d,%d,%d\n",restore[0],restore[1],restore[2],restore[3]);
        return 0;

}

/*
 * REMOVE MODULE
 * Restore the system call table to its original form removing our custom system  calls
 */

void cleanup_module(void) {

        unsigned long cr0;

        /*
         * In order to restore the system call table, the WRITE-PROTECTED
         * MODE has to be disabled again temporarily, so first save the
         * value of the CR0 register
         */

        disable_write_protected_mode(&cr0);

        /*
         * Restore system call table to its original shape
         */

        //system_call_table[restore[0]]=not_implemented_syscall;
        //system_call_table[restore[1]]=(unsigned long)sys_ni_syscall;
        //system_call_table[restore[2]]=(unsigned long)sys_ni_syscall;
        //system_call_table[restore[3]]=(unsigned long)sys_ni_syscall;

        /*
         * Restore value of register CR0
         */

        enable_write_protected_mode(&cr0);

        /*
         * Free memory allocated for the ipc_ids structure
         */

        kfree(barrier_ids);

        printk(KERN_INFO "Released memory allocated for the structure ipc_ids at address %lu\n",barrier_ids);

        printk(KERN_INFO "Module \"barrier_module\" removed\n");

}

/*
 * INSERT/REMOVE MODULE - end
 */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Davide Leoni");
