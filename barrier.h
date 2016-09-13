#ifndef BARRIERSYNCHRONIZATION_BARRIER_H
#define BARRIERSYNCHRONIZATION_BARRIER_H

/*
 * The possible flags that can be used when
 * a new barrier is requested:
 *
 * BARRIER_CREATE: the barrier has to be created if it doesn't exist
 * BARRIER_EXCL: an error code has to be returned if BARRIER_CREATE is invoked and the barrier already exists
 *
 */

#define BARRIER_CREATE (IPC_CREAT)
#define BARRIER_EXCL (IPC_EXCL)
#define BARRIER_PRIVATE (IPC_PRIVATE)

/*
 * The number of PRIORITY SYNCHRONIZATION TAGS: legal values for these
 * tags go from 0 to BARRIER_TAGS-1
 */

#define BARRIER_TAGS 32

/*
 * Maximum number of IDs for the barrier synchronization object: the same
 * limit as for semaphores
 */

#define BARRIER_IDS_MAX (IPCMNI)

/*
 * Kernel service routine to get the an instance of a barrier.
 * Paremeters:
 * 1- key_t key: used to univokely identify the instance of the barrier
 * 2- int flags: flags to define the action to perform on the barrier
 */

asmlinkage long sys_get_barrier(key_t key,int flags);

/*
 * Initialize the "ids_namespace" structure to be used to handle ids of the
 * varieous instances of the barrier
 */

//void ipc_init_ids(struct ipc_ids *);

/*
 * Simplified custom version of the "kern_ipc_perm" structure used by
 * the IPC subsystem to handle metadata related to an instance of an
 * IPC synchronization object.
 * Unlike "kern_ipc_perm", we don't care about user permissions
 */

struct barrier_ipc_perm
{
        spinlock_t	lock;
        int		id;
        key_t		key;
        umode_t		mode;
        unsigned long	seq;
};

/*
 * Custom version of the "ipc_params" structure used in the IPC API
 * to hold parameters necessary for IPC operations: we only need the
 * flags and the key as parameters
 */

struct barrier_params
{
        key_t key;
        int flg;
};

/*
 * Structure representing a new barrier: the most important field is the array
 * with 32 elements, one per SYNCHRONIZATION TAG, where each element
 * is a list of pointers to wait queues for that specific TAG, one per process
 * which requested to sleep on the barrier with that priority tag. The wait queue
 * is stored in the kernel mode stack of the process, because we don't want to
 * allocate more memory than what is already allocated by the stack. Of course
 * we have to keep the level of stack occupancy low, but since we have one wait
 * queue for each synchronization tag, and each wait queue has only one element,
 * we occupy at most 32*size_of(wait_queue) for each barrier in the kernel mode
 * stack of a process
 */

struct barrier_struct{

        /*
         * barrier_ipc_perm structure corresponding to the barrier: its main field
         * is the ID that is assigned to (and only to) the instance ob barrier by
         * the ipc_ids structure
         */

        struct barrier_ipc_perm	barrier_perm;

        /*
         * This array contains the list of pointers to wait queues, with one list for
         * each SYNCHRONIZATION TAG.Each list contains the address of a wait queue for
         * each process that wants to sleep on the barrier with the SYNCHRONIZATION TAG
         * corresponding to the list; the wait queue is stored in the kernel mode stack
         * of the process itself
         */

        struct list_head queues [BARRIER_TAGS];
};

/*
 * Structure that holds some ipc operations. This structure is used to unify
 * the calls to sys_msgget(), sys_semget(), sys_shmget()
 *      . routine to call to create a new ipc object. Can be one of newque,
 *        newary, newseg
 *      . routine to call to check permissions for a new ipc object.
 *        Can be one of security_msg_associate, security_sem_associate,
 *        security_shm_associate
 *      . routine to call for an extra check if needed
 */
struct ipc_ops {
        int (*getnew) (struct ipc_namespace *, struct barrier_params *);
        int (*associate) (struct kern_ipc_perm *, int);
        int (*more_checks) (struct kern_ipc_perm *, struct barrier_params *);
};


#endif //BARRIERSYNCHRONIZATION_BARRIER_H
