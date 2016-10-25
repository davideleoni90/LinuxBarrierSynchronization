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
 * Maximum number of processes synchronized on a tag: we want to avoid that
 * list of wait queue heads grow indefinitely
 */

#define BARRIER_PER_TAG_MAX 128

/*
 * Maximum number of IDs for the barrier synchronization object: there can be at most
 * BARRIER_IDS_MAX different instances at a time
 */

#define BARRIER_IDS_MAX 128

/*
 * Kernel service routine to get the an instance of a barrier.
 * Parameters:
 * 1- key_t key: used to uniquely identify the instance of the barrier
 * 2- int flags: flags to define the action to perform on the barrier
 */

asmlinkage long sys_get_barrier(key_t key,int flags);

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

struct ipc_params{
        key_t key;
        int flg;
        union{
                size_t  size;
                int nsems;
        }u;
};

/*
 * Element associated to a process sleeping on a certain tag
 *
 * queue_list: list element, used to connect the structure to the list "queues" within the barrier_tag
 * structure
 *
 * queue: pointer to the wait queue head of the wait queue stored in the Kernel Mode Stack of a sleeping
 * process synchronized on this tag
 */

struct process_queue
{
        struct list_head queue_list;
        wait_queue_head_t* queue;
};

/*
 * Structure that keeps track of all the processes sleeping on a certain synchronization
 * tag
 *
 * counter: number of processes sleeeping on this tag; this has to be smaller than the limit
 * represented by the constant BARRIER_PER_TAG_MAX
 *
 * tag: the synchronization tag corresponding to this structure
 *
 * tag_list: list element, used to connect the structure to the list "tags" within the barrier
 *
 * queues: head of a list of structures, each containing a pointer to the wait queue into which
 * a process is sleeping in order to synchronize itself on the this tag
 *
 * sleeping: boolean value that has to be changed from the "sleep_on_barrier" system call in order
 * to wake up all the processes synchronized on the this tag
 *
 *
 */

struct barrier_tag
{
        int counter;
        int tag;
        struct list_head tag_list;
        struct list_head queues;
        bool sleeping;
};

/*
 * Structure representing a barrier
 *
 * barrier_perm: permission object associated to the barrier: its main field
 * is the ID that is assigned to (and only to) the instance of barrier by
 * the ipc_ids structure
 *
 * tags: head of the list of "barrier_tag" structures, one for each different TAG
 * requested using the system call "sleep_on_barrier(bd,TAG)"
 */

struct barrier_struct{

        struct kern_ipc_perm barrier_perm;
        struct list_head tags;
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
        int (*getnew) (struct ipc_namespace *, struct ipc_params *);
        int (*associate) (struct kern_ipc_perm *, int);
        int (*more_checks) (struct kern_ipc_perm *, struct ipc_params *);
};


#endif //BARRIERSYNCHRONIZATION_BARRIER_H
