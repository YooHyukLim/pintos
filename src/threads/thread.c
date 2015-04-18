#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

int load_avg;              /* Load avg of this program. */
int ready_threads;         /* The count of number of thread
                              in readylist. */

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
static struct list ready_list_mlfqs[PRI_MAX + 1];

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  int i;

  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);

  if (!thread_mlfqs)
    list_init (&ready_list);
  else
    for (i=PRI_MIN; i<=PRI_MAX; i++)
      list_init (&ready_list_mlfqs[i]);

  list_init (&all_list);

  load_avg = 0;
  ready_threads = 0;

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread || !strcmp ("idle", t->name))
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  /* When creating new thread, if the priority of new thread is
     bigger than current thread's, immediately yield */
  if (t->priority > thread_get_priority ())
    thread_yield ();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  struct thread *cur_t = thread_current ();
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  cur_t->status = THREAD_BLOCKED;

  /* If the thread blocked is not idle thread, then substact 1
     from ready threads. */
  if (cur_t != idle_thread || strcmp (cur_t->name, "idle"))
    ready_threads--;

  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data.

   When add the unblock tread to the ready list, push by priority. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
 
  if (!thread_mlfqs)
    thread_push_by_priority (&ready_list, t);
  else
    /* If using mlfqs, use arry list for ready list rather than
       using list for ready list. */
    list_push_back (&ready_list_mlfqs[t->priority], &t->elem);
  //list_push_back (&ready_list, &t->elem);
  t->status = THREAD_READY;

  /* If the thread unblocked is not idle thread, then
     add 1 ready_threads. */
  if (t != idle_thread || strcmp (t->name, "idle")) {
    ready_threads++;
  }
  
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  /* When exit from thread, also substract 1 from ready_thread. */
  ready_threads--;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) { 
    if (!thread_mlfqs)
      thread_push_by_priority (&ready_list, cur);
    else
      list_push_back (&ready_list_mlfqs[cur->priority], &cur->elem);
    //list_push_back (&ready_list, &cur->elem);
  }
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
  {
    struct thread *t = list_entry (e, struct thread, allelem);
    func (t, aux);
  }
}

/* When use mlfqs, calculate the priority by recent_cpu and nice.
   Use shift arithmetic rather than use multiple FIXED_COEF. */
void
thread_cal_priority (struct thread *t)
{
  int i;
  int priority = t->priority;

  if (t == idle_thread || !strcmp (t->name, "idle"))
    return;
  
  /* 2^14 = 4 * FIXED_COEF
     Use the priority fomula given to calculate new priority. */
  t->priority = PRI_MAX - (t->recent_cpu >> 16) - t->nice * 2;

  /* Because of recent cpu and nice, new priority can be over or
     underflow from limit. Then set priority PRI_MAX or PRI_MIN
     properly. */
  if (t->priority > PRI_MAX)
    t->priority = PRI_MAX;
  else if (t->priority < PRI_MIN)
    t->priority = PRI_MIN;

  
  if (t->priority != priority) {
    if (!intr_context()) {

      /* If this function wasn't called from interrupt, then
         check there is a thread which has higher priority than
         current thread. If then, yield this thread. */
      for (i = priority; i > t->priority; i--)
        if (!list_empty (&ready_list_mlfqs[i])) {
          thread_yield ();
          break;
        }

    } else {

      /* If this function was called from interrupt, do proper
         action following the thread's status. */
      if (t->status == THREAD_RUNNING)

        /* If the status of the thread is running, it means there
           can be one or more thread which has higher priority
           than this thread. So check it, and if there is,
           then yield it. */
        for (i = priority; i > t->priority; i--)
          if (!list_empty (&ready_list_mlfqs[i])) {
            intr_yield_on_return ();
            break;
          }

      /* If the status of this thread is ready or blocked,
         then reposition to proper place. */
      else if (t->status == THREAD_READY)
        list_push_back (&ready_list_mlfqs[t->priority], &t->elem);
      else if (t->status == THREAD_BLOCKED)
        thread_set_priority_and_repos (t, t->priority);

    }
  }
}
/* Set the priority to the selected thread and reposition
   the thread in list by priority. 
   This function doesn't care about list's type. */
void
thread_set_priority_and_repos (struct thread *t, int new_priority)
{
  struct list_elem *pos = NULL;
  struct list_elem *cur = &t->elem;

  if (t->elem.prev == NULL)
    return;

  /* Set the new priority. */
  t->priority = new_priority;

  /* Check whether the prev of the thread is head of the list or not.
     Or check whether the priority of prev is larger than the selected
     thread's. */
  pos = t->elem.prev;
  if (pos->prev == NULL
      || list_entry (pos, struct thread, elem)->priority
        > new_priority)
    return;

  /* First of all, delete the elem of the thread from the list. */
  cur->prev->next = cur->next;
  cur->next->prev = cur->prev;

  /* Find where the thread be positioned in the list by priority.
     The priority of the thread must be small than its prev's. */
  for (pos = pos->prev; pos->prev != NULL; pos = pos->prev) {
    if (list_entry (pos, struct thread, elem)->priority
        >= new_priority)
      break;
  }

  /* Set the position. */
  cur->prev = pos;
  cur->next = pos->next;
  pos->next->prev = cur;
  pos->next = cur;
}

/* This function is same as thread_set_priority, but it doesn't care
   of whether the priority is original or not. */
void
thread_set_current_priority (int new_priority)
{
  thread_current ()->priority = new_priority;
  
  /* Apply the priority and rescheduling.*/
  if (!list_empty (&ready_list)
      && new_priority
        < list_entry (list_begin (&ready_list), struct thread, elem)->priority)
    thread_yield ();
}

/* Sets the current thread's priority to NEW_PRIORITY. If the priority is not
   original, find the address of original and fix it. */
void
thread_set_priority (int new_priority) 
{
  struct thread *t = thread_current ();
  struct lock_elem *le = NULL;

  /* If thread holds locks, get the priority of the oldest lock. */
  if (!list_empty (&t->priority_stack))
    le = list_entry (list_rbegin (&t->priority_stack), struct lock_elem, elem);
  
  /* Set priority to the origianl priority. */
  if (le == NULL)
    t->priority = new_priority;
  else
    le->priority = new_priority;
  
  /* Apply the priority and rescheduling.*/
  if (!list_empty (&ready_list)
      && new_priority
        < list_entry (list_begin (&ready_list), struct thread, elem)->priority)
    thread_yield ();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
  struct thread *cur_t = thread_current ();
  cur_t->nice = nice;

  thread_cal_priority (cur_t);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current ()->nice;
}

/* Calculate new load avg by the fomula given. */
void
thread_cal_load_avg (void)
{
  ASSERT (intr_get_level () == INTR_OFF);
  /* 2 * 8055 = 16110 = Fixed-Point Real Number of 59/60. */
  /* 273 = Fixed-Point Real Number of 1/60. */
  /* Used shift arithmetic opration rather than use
     multiplication. */
  load_avg = ((((int64_t) 8055) * load_avg) >> 13)
              + (273 * ready_threads);
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  int result = (load_avg * 25) << 2;
  /* 8192 = FIXED_COEF / 2 */
  return result >= 0 ? (result + 8192) >> 14 : (result - 8192) >> 14;
}

/* Calculate new recent cpu by the fomula given. */
void
thread_cal_recent_cpu (struct thread *t)
{
  int res;

  /* If current thread is idle thread, do nothing. */
  if (t == idle_thread || !strcmp (t->name, "idle"))
    return;

  /* Used shift arithmetic operation rather than use
     multiplication. */
  res = load_avg << 1;
  res = (((int64_t) res) << 14) / (res + FIXED_COEF);

  t->recent_cpu = ((((int64_t) res) * t->recent_cpu)
                    >> 14) + (t->nice << 14);
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  int recent_cpu = (thread_current ()->recent_cpu * 25) << 2;
  /* 8192 = FIXED_COEF / 2 */
  return recent_cpu >= 0 ? (recent_cpu + 8192) >> 14 : (recent_cpu - 8192) >> 14;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  idle_thread->priority = PRI_MIN;
  sema_up (idle_started);

  for (;;) 
  {
    /* Let someone else run. */
    intr_disable ();
    thread_block ();

    /* Re-enable interrupts and wait for the next one.

       The `sti' instruction disables interrupts until the
       completion of the next instruction, so these two
       instructions are executed atomically.  This atomicity is
       important; otherwise, an interrupt could be handled
       between re-enabling interrupts and waiting for the next
       one to occur, wasting as much as one clock tick worth of
       time.

       See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
       7.11.1 "HLT Instruction". */
    asm volatile ("sti; hlt" : : : "memory");
  }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;
  struct thread *cur_t = NULL;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;

  /* Initialize lock_acquired and priority_stack. */
  list_init (&t->priority_stack);
  t->lock_acquired = NULL;

  if (thread_mlfqs) {
    /* Initialize nice, recent_cpu, value. */
      
    if (!strcmp ("main", name)) {
      /* If the thread made is "main", initialize all value 0. */
      t->nice = 0;
      t->recent_cpu = 0;

      /* Because the values of nice, recent_cpu, load_avg are 0,
         the priority is PRI_MAX. */
      t->priority = PRI_MAX;
    } else if (strcmp ("idle", name)) {
      /* If the thread made isn't "main" and "idle", set all value to
         the value of parent. */
      cur_t = thread_current ();
      t->nice = cur_t->nice;
      t->recent_cpu = cur_t->recent_cpu;
      t->priority = PRI_MAX - t->recent_cpu / 65536 - t->nice * 2;
      if (t->priority > PRI_MAX)
        t->priority = PRI_MAX;
      else if (t->priority < PRI_MIN)
        t->priority = PRI_MIN;

    } else {
      /* If the thread is idle thread, set priority PRI_MIN. */
      t->priority = PRI_MIN;
    }

  }

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  int i;
  if (!thread_mlfqs) {

    if (list_empty (&ready_list))
      return idle_thread;
    else
      return list_entry (list_pop_front (&ready_list),
                          struct thread, elem);

  } else {

    /* If use mlfqs, get the thread from the first item of ready list
       which has a high priroity. */
    for (i=PRI_MAX; i>=PRI_MIN; i--) {
      if (!list_empty (&ready_list_mlfqs[i]))
        return list_entry (list_pop_front (&ready_list_mlfqs[i]),
                            struct thread, elem);
    }

    return idle_thread;
  }
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next) {
    prev = switch_threads (cur, next);
  }
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* thread_push_by_priority will add a thread to ready_list according to
   its priority. The thread will be positioned behind of the thread
   which has bigger priority than new thread, and in front of the thread
   which has smaller or same priority. */
void
thread_push_by_priority (struct list *list, struct thread *cur_t)
{
  struct list_elem *pos = NULL;
  struct list_elem *end = list_end (list);
  struct thread *t = NULL;
  int cur_priority = cur_t->priority;

  for (pos = list_begin (list); pos != end; pos = pos->next) {

    t = list_entry (pos, struct thread, elem);

    if (cur_priority > t->priority)
      break;
  }

  list_insert (pos, &cur_t->elem);
}


/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
