/*****************************************************************************/

#ifndef _os2_semaphore_
#define _os2_semaphore_

typedef struct _OS2SEM {
    HMTX    hmtx;
    HEV     hev;
    ULONG   cnt;
} OS2SEM;

typedef OS2SEM * sem_t;

int sem_init(sem_t *sem, int pshared, unsigned value);
int sem_wait(sem_t *sem);
int sem_post(sem_t *sem);
int sem_destroy(sem_t *sem);

#endif
/*****************************************************************************/
