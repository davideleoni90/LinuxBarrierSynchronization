#ifndef BARRIERSYNCHRONIZATION_HELPER_C_H
#define BARRIERSYNCHRONIZATION_HELPER_C_H

#define WP_X86 0x00010000

/*
 * Address of "sys_ ni_syscall": system call corresponding
 * to free entries of the system call table
 * THIS HAS TO BE TAKEN FROM THE SYSTEM MAP BECAUSE THE SYMBOL
 * IS NOT EXPORTED
 */

unsigned long not_implemented_syscall=3222697504;

/*
 * ADDRESSES OF FUNCTIONS FROM THE SYSTEM V IPC SUSBSYSTEM - start
 *
 * -> these functions are not exported so their address have to be
 * taken from the System map
 */

/*
 * This functions initializes a structure "ipc_ids": this keeps track
 * of all the instances of barrier within the system; each of them is
 * assigned an id provided from the IDR id management system
 */

void (*ipc_init_ids)(struct ipc_ids* ids)=(void (*)(struct ipc_ids* ids))3224296752;

/*
 * This function returns a new IPC synchronization object. The parameters given
 * determine the type and the features of the object that is going to be created
 * and they are:
 *
 * 1-key: this is an identifier that will be associated to the object
 * 2-flags: these determine some features of the object, for instance if it has
 * to created from scratch or if one with the same id has to be returned
 * 3-operations: callback functions associated to the operations of the object,first
 * of all its creation
 * 4-ipc_ids: reference to the structure used to manage all the instances of a certain
 * kind of synchronization object; there's one for all IPC semaphores,one for all IPC
 * shared memories and one for all IPC message queues.
 * 4-ipc_namespace:
 */

int (*ipcget)(struct ipc_namespace *ns, struct ipc_ids *ids,struct ipc_ops *ops, struct ipc_params *params)=(int (*)(struct ipc_namespace *ns, struct ipc_ids *ids,struct ipc_ops *ops, struct ipc_params *params))3224296816;

/*
 * This function is used to allocate memory for a new IPC synchronization object: since
 * RCU is used to guarantee parallelism of usage of these objects, when one is allocated
 * a structure "rcu_head" has to be prepended.
 * The pointer to the allocated object is returned
 */

void* (*ipc_rcu_alloc)(int size)=(void* (*)(int size))3224298384;

/*
 * Add an entry 'new' to the IPC ids idr. The permissions object is
 * initialised and the first free entry is set up and the id assigned
 * is returned. The 'new' entry is returned in a locked state on success.
 * On failure the entry is not locked and a negative err-code is returned.
 * the ID is returned.
 */

int (*ipc_addid)(struct ipc_ids* ids, struct kern_ipc_perm* new, int size)=(int (*)(struct ipc_ids* ids, struct kern_ipc_perm* new, int size))3224296272;

/*
 * Remove from the idr of the given ipc_ids the entry corresponding to the given permission object
 *
 * The function "idr_remove" from the IDR API is invoked and also the counter of ids in use from the
 * ipc_ids structure is decreased
 */

void (*ipc_rmid)(struct ipc_ids *ids, struct kern_ipc_perm *ipcp)=(void (*)(struct ipc_ids *ids, struct kern_ipc_perm *ipcp))3224296192;

/*
 * Get rid of the object whose address is given as parameter: this is done
 * waiting for all the readers (if any), that might have references to the object,
 * complete
 */

void (*ipc_rcu_putref)(void *ptr)=(void (*)(void *ptr))3224295984;

/*
 * Looks for the permission object corresponding to the given id within the IDR object of the given ipc_ids structure:
 * if it is found, this is returned, otherwise error code -EINVAL (Invalid Argument) is returned
 */

struct kern_ipc_perm *(*ipc_lock_check)(struct ipc_ids *ids, int id)=(struct kern_ipc_perm *(*)(struct ipc_ids *ids, int id))3224297968;

/*
 * ADDRESSES OF FUNCTIONS FROM THE SYSTEM V IPC SUSBSYSTEM - end
 */

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
void enable_write_protected_mode(unsigned long* cr0);
void disable_write_protected_mode(unsigned long* cr0);

#endif //BARRIERSYNCHRONIZATION_HELPER_C_H
