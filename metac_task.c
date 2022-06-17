#include "metac_task.h"
#include <assert.h>
#include <stdlib.h>

#if !defined(ATOMIC)
#define INC(v) \
    (v++)
#elif __GNUC__
#  define INC(v) \
    (__builtin_atomic_fetch_add(&v, __ATOMIC_RELEASE))
#elif _MSC_VER
#  define INC(v) \
  _InterlockedIncrement(&v);
#else
#  error("No atomic supported")
#endif
// the watcher shoud allocate the worker contexts since it is responsible for distribution
// and monitoring of the work

#ifndef __STDC_NO_THREADS__
# include <threads.h>
#else
#  include "3rd_party/tinycthread/tinycthread.c"
#endif


static bool watcherIntialized = 0;

_Thread_local worker_context_t* threadContext = 0;

taskqueue_t gQueue;

_Thread_local void *_CurrentFiber;

void* CurrentFiber()
{
    return _CurrentFiber;
}

void Taskqueue_Init(taskqueue_t* queue)
{
    queue->queueMemory = cast(task_t (*)[1024])
        calloc(sizeof(task_t), ARRAY_SIZE(*queue->queueMemory));
#if THREAD_MUTEX
    mtx_init(&queue->TicketLock.Mutex);
#endif
}

void FiberPool_Init(fiber_pool_t* self)
{
    for(uint32_t i = 0; i < sizeof(self->FreeBitfield) * 8; i++)
    {

        self->fibers[i] = acl_fiber_create(0, 0, 128 * 1024);
        // self->contexts[i];
    }

}

// the actual worker function
void workerFunction(worker_context_t* context)
{
    threadContext = context;
    Taskqueue_Init(&context->Queue);

    fiber_pool_t fiberPool;

    FiberPool_Init(&fiberPool);
    //context->FiberPool = &fiberPool;
    uint32_t* fiberExecCounts = calloc(sizeof(uint32_t), FIBERS_PER_WORKER);
    for(;;)
    {

    }
}

/// Return Value thrd_success thingy
uint32_t MakeWorkerThread(void (*workerFunc)(worker_context_t*), worker_context_t* workerContext)
{
    static uint32_t workerId = 1;
    workerContext->WorkerId = INC(workerId);

//    void (*threadProc)(void*) = (void (*)(void*)) workerFunc;
    return thrd_create(&workerContext->Thread, workerFunc, workerContext);
}


void TaskSystem_Init(tasksystem_t* self, uint32_t workerThreads, void (*workerFn)(worker_context_t*))
{
    // gQueue
    self->workerContexts = cast(worker_context_t*)
        calloc(sizeof(worker_context_t), workerThreads);
    //self->workerContexts +
    for(uint32_t i = 0; i < workerThreads; i++)
    {
        worker_context_t* ctx = self->workerContexts + i;

        MakeWorkerThread(workerFunction, ctx);
    }
    //thrd_creat

    self->nWorkers = workerThreads;
}
#define QUEUE_CUTOFF 960

static inline uint32_t tasksInQueue(const uint32_t readP, const uint32_t writeP)
{
    if (writeP >= readP)
    {
        return (writeP - readP);
    }
    else
    {
        // wrap-around
        // we go from readP to length and from zero to writeP
        return ((TASK_QUEUE_SIZE - readP) + writeP);
    }
}

#define ATOMIC_LOAD(VAR_PTR) \
    *(VAR_PTR)

bool ServingMe(volatile ticket_lock_t* lock, uint32_t myTicket)
{
#ifdef THREAD_MUTEX
    bool result = mtx_trylock(&lock->Mutex) == thrd_success;
#else
    bool result = ATOMIC_LOAD(&lock->currentlyServing) == myTicket;
#endif
#ifdef INSPECTOR
    if (result)
        __itt_sync_acquired(lock);
#endif
    return result;
}

void ReleaseTicket(volatile ticket_lock_t* lock, uint32_t ticket)
{
#ifdef INSPECTOR
    __itt_sync_releasing(lock);
#endif
    assert(lock->currentlyServing == ticket);
    INC(lock->currentlyServing);
#ifdef THREAD_MUTEX
    mtx_unlock(&lock->Mutex);
#endif
}
uint32_t DrawTicket(volatile ticket_lock_t* lock)
{
#ifdef INSPECTOR
    __itt_sync_prepare(lock);
#endif
    return INC(lock->nextTicket);
}

uint32_t TaskQueue_TasksInQueue_(taskqueue_t* self)
{
    return tasksInQueue(self->readPointer, self->writePointer);
}
#ifdef X86
#  define FENCE asm volatile ("mfence" ::: "memory");
#  define MM_PAUSE asm volatile ("pause");
#else
#  define FENCE
#  define MM_PAUSE
#endif
// Returns true if task was pushed
//         false if the queue was already full
bool TaskQueue_Push(taskqueue_t* self, task_t* task)
{
    // if this is true the Queue is full
    uint32_t readPointer = self->readPointer & (TASK_QUEUE_SIZE - 1);
    uint32_t writePointer = self->writePointer & (TASK_QUEUE_SIZE - 1);
    if (readPointer == writePointer + 1)
        return false;
    FENCE
    uint32_t myTicket = DrawTicket(&self->TicketLock);
    FENCE
    while (!ServingMe(&self->TicketLock, myTicket))
    {
        MM_PAUSE
    }
    FENCE
    readPointer = self->readPointer & (TASK_QUEUE_SIZE - 1);
    writePointer = self->readPointer & (TASK_QUEUE_SIZE - 1);
    if (readPointer == writePointer + 1)
    {
        ReleaseTicket(&self->TicketLock, myTicket);
        return false;
    }

    ((*self->queueMemory)[(writePointer + 1 <= TASK_QUEUE_SIZE) ? writePointer : 0]) = *task;
    INC(writePointer);
    FENCE
    ReleaseTicket(&self->TicketLock, myTicket);
    return true;
}

bool TaskQueue_Pull(taskqueue_t* self, task_t** taskP)
{
    uint32_t readPointer = self->readPointer & (TASK_QUEUE_SIZE - 1);
    uint32_t writePointer = self->writePointer & (TASK_QUEUE_SIZE - 1);

    // check if queue is empty
    if (readPointer == writePointer)
        return false;

    FENCE
    uint32_t myTicket = DrawTicket(&self->TicketLock);
    FENCE
    while (!ServingMe(&self->TicketLock, myTicket))
    {
        MM_PAUSE
    }
    FENCE

    if (readPointer == writePointer)
    {
        ReleaseTicket(&self->TicketLock, myTicket);
        return false;
    }

    *taskP = self->queueMemory[INC(readPointer)];

    FENCE
    ReleaseTicket(&self->TicketLock, myTicket);
    return true;
}

bool AddTask(task_t* task)
{
    bool result = false;

    taskqueue_t* preferredQ =
        threadContext ? &threadContext->Queue : &gQueue;
    if (TaskQueue_TasksInQueue_(preferredQ) < QUEUE_CUTOFF)
    {
        result = TaskQueue_Push(preferredQ, task);
    }
    //TODO try pushing into other queues and such things

    return result;
}
