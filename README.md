# 1. The Scheduler Entry Point

## 1.1 schedule()

The main entry point for scheduling is:

    schedule()

Source file:

    kernel/sched/core.c

When a task voluntarily gives up the CPU, blocks while waiting for an event, or is required to reschedule, the kernel enters the scheduling path.

The scheduler must determine which task should execute next.

However, Linux supports multiple scheduling classes. Therefore, the scheduler cannot simply ask CFS to select a task every time.

It must first determine which scheduling class has the highest-priority runnable task.

## 1.2 Scheduler Classes

Linux 5.15 organizes scheduling policies into scheduling classes.

The important classes, in scheduling priority order, are:

    Stop class
        |
    Deadline class
        |
    Real-time class
        |
    Fair class
        |
    Idle class

Examples:

| Scheduling class | Policies / purpose |
|---|---|
| Stop | Special CPU stopper tasks |
| Deadline | SCHED_DEADLINE |
| Real-time | SCHED_FIFO, SCHED_RR |
| Fair | SCHED_NORMAL, SCHED_BATCH |
| Idle | Per-CPU idle task |

The scheduler examines these classes in priority order.

A runnable task in a higher-priority scheduling class takes precedence over tasks in lower-priority classes.

For example:

If a real-time task is runnable, it takes precedence over an ordinary SCHED_NORMAL task.

If no higher-priority class has a runnable task, the scheduler can select a task from the fair class.

If no suitable task is available, the idle class provides the idle task.

## 1.3 pick_next_task()

The generic scheduler uses:

    pick_next_task()

The function is responsible for selecting the next task through the scheduling-class interface.

Conceptually:

    schedule()
        |
        v
    __schedule()
        |
        v
    pick_next_task()
        |
        v
    Check scheduling classes
        |
        +---- Stop
        |
        +---- Deadline
        |
        +---- Real-time
        |
        +---- Fair
        |
        +---- Idle
        |
        v
    Selected task

Important:

`pick_next_task()` is generic with respect to scheduling classes.

It does not itself implement the CFS red-black tree selection algorithm.

Instead, it asks the appropriate scheduling class to select its next task.

Linux 5.15 also contains an optimized path for cases where the fair class can be selected directly.

## 1.4 CFS Selection Path

When the fair scheduling class is selected, its implementation eventually uses:

    pick_next_task_fair()

The CFS selection process involves:

    pick_next_task_fair()
        |
        v
    pick_next_entity()
        |
        v
    __pick_first_entity()
        |
        v
    Leftmost entity in red-black tree
        |
        v
    Selected scheduling entity

There is an important distinction between the two helper functions:

    __pick_first_entity()

Returns the leftmost entity in the CFS red-black tree.

    __pick_next_entity()

Returns the next entity in tree order after a supplied entity, using `rb_next()`.

`pick_next_entity()` combines the leftmost entity with additional selection logic, including the current entity and scheduling buddies.

Therefore, the simplified statement that CFS always executes the leftmost tree node is useful for understanding the basic principle, but it is not a complete description of the Linux 5.15 implementation.

## 1.5 Why Does CFS Use the Leftmost Node?

The CFS tree is ordered by virtual runtime.

The entity with the smallest virtual runtime is located at the leftmost position.

For example:

                  20
                 /
               10
              /
             5

The entity with vruntime 5 is the leftmost entity.

CFS maintains a cached reference to this node.

Therefore, finding the leftmost entity does not require traversing the entire tree.

The operation uses:

    rb_first_cached()

This provides constant-time leftmost-node lookup.

---

# 2. CFS Red-Black Tree

## 2.1 What Is vruntime?

Every CFS scheduling entity maintains a virtual runtime value.

The virtual runtime measures execution progress after accounting for scheduling weight.

Conceptually:

    vruntime_delta =
        execution_time * NICE_0_LOAD / entity_weight

Where:

    execution_time = Actual execution time
    NICE_0_LOAD    = 1024
    entity_weight  = Scheduling weight

A higher scheduling weight causes vruntime to increase more slowly.

A lower scheduling weight causes vruntime to increase more quickly.

CFS uses virtual runtime to maintain fairness between scheduling entities.

## 2.2 Why a Red-Black Tree?

The scheduler needs to maintain runnable entities according to vruntime.

It must support:

- Insertion of newly runnable entities.
- Removal of entities that stop being runnable.
- Efficient identification of the smallest vruntime.

A red-black tree is a self-balancing binary search tree.

Each node is ordered using vruntime as its key.

The tree maintains:

    Left subtree  <  Parent  <  Right subtree

The red-black balancing rules keep the tree height logarithmic.

For n nodes:

    Insertion          O(log n)
    Deletion           O(log n)
    Search             O(log n)
    Cached leftmost    O(1)

## 2.3 Adding Entities

When an entity becomes runnable, CFS can insert it into the tree.

Relevant Linux 5.15 functions:

    enqueue_entity()
    __enqueue_entity()

The insertion process:

1. Compare the entity's vruntime with existing nodes.
2. Find the appropriate insertion position.
3. Link the new node.
4. Restore red-black tree properties.
5. Update the cached leftmost pointer if required.

Important operations:

    rb_link_node()
    rb_insert_color_cached()

## 2.4 Removing Entities

When an entity is removed from the runnable tree, CFS uses:

    dequeue_entity()
    __dequeue_entity()

The cached red-black tree API provides:

    rb_erase_cached()

This removes the node and maintains the tree's balancing properties and leftmost cache.

The entity can subsequently become runnable again and be inserted through the enqueue path.

---

# 3. Sleeping and Waking Up

## 3.1 Why Do Tasks Sleep?

Consider an application that requests data from a disk.

The application issues:

    read()

The data might not be immediately available.

The task must wait until the requested operation completes.

Another example is a process waiting for keyboard input.

The kernel must avoid allowing the task to consume CPU time continuously while waiting.

If the task repeatedly checks whether the event has occurred, it performs busy waiting.

This wastes CPU resources.

Instead, Linux allows the task to enter a sleeping state.

While sleeping, the task is not runnable and another runnable task can use the CPU.

## 3.2 Sleeping Task States

Two important sleeping states are:

    TASK_INTERRUPTIBLE
    TASK_UNINTERRUPTIBLE

### TASK_INTERRUPTIBLE

The task sleeps while waiting for an event.

It can also be awakened by an applicable signal.

Examples:

- Waiting for input.
- Waiting for an event.
- Waiting for certain I/O operations.

### TASK_UNINTERRUPTIBLE

The task sleeps while waiting for an event but does not normally respond to signals that interrupt interruptible sleep.

An example is certain types of kernel I/O waiting.

Both states represent non-runnable tasks.

The task is not eligible to receive CPU time until it becomes runnable again.

## 3.3 Sleeping Process Flow

Conceptually:

    Running task
         |
         v
    Event not available
         |
         v
    Set sleeping state
         |
         v
    Add to wait queue
         |
         v
    schedule()
         |
         v
    Another runnable task executes

The sleeping task is removed from the runnable scheduling queue when it is deactivated.

For a normal fair-class task, this means its scheduling entity is removed from the CFS runnable tree.

Important distinction:

The task is not destroyed.

Its task_struct still exists.

It is simply not runnable.

---

# 4. Wait Queues

## 4.1 What Is a Wait Queue?

A wait queue is a kernel mechanism used to maintain tasks waiting for a particular event.

For example:

A process requests data from a device.

The device operation is not complete.

The process waits on a queue until the required event occurs.

The kernel uses wait queues to coordinate sleeping and waking.

The relevant structure is:

    struct wait_queue_head

It contains a list of wait entries and synchronization mechanisms.

Each waiting task is represented through a wait queue entry.

## 4.2 Creating a Wait Queue

A wait queue can be declared statically:

    DECLARE_WAIT_QUEUE_HEAD(my_wait_queue);

Or initialized dynamically:

    init_waitqueue_head(&my_wait_queue);

The queue is shared between the code that waits for an event and the code that signals its occurrence.

## 4.3 Creating a Wait Queue Entry

A task can create a wait entry using:

    DEFINE_WAIT(wait);

The entry represents the task's participation in the wait queue.

It can be added using:

    add_wait_queue(&my_wait_queue, &wait);

The wait queue now contains an entry for the waiting task.

## 4.4 Preparing to Sleep

The kernel provides:

    prepare_to_wait()

Conceptually:

    prepare_to_wait(&my_wait_queue,
                    &wait,
                    TASK_INTERRUPTIBLE);

This operation:

1. Adds the wait entry if it is not already present.
2. Sets the current task's state to the requested sleeping state.

The ordering and synchronization are important because they prevent races between checking the event and going to sleep.

## 4.5 The Waiting Loop

A typical conceptual waiting loop looks like this:

    DEFINE_WAIT(wait);

    for (;;) {

        prepare_to_wait(&my_wait_queue,
                        &wait,
                        TASK_INTERRUPTIBLE);

        if (condition)
            break;

        schedule();
    }

    finish_wait(&my_wait_queue, &wait);

The logic is:

1. Prepare the task for sleeping.
2. Check whether the required condition is true.
3. If the condition is false, call `schedule()`.
4. The scheduler selects another runnable task.
5. When awakened, the task resumes execution.
6. Check the condition again.
7. Exit when the condition becomes true.
8. Remove the wait entry using `finish_wait()`.

In real kernel code, signal handling and other error conditions must also be handled where appropriate.

## 4.6 Why Is the Condition Checked Again?

A task can wake up without the event it was waiting for actually occurring.

This is called a spurious wake-up.

For example:

A task is waiting for data.

Another task wakes it.

However, the required data is still unavailable.

The awakened task must check the condition again.

Therefore, waiting is generally implemented using a loop rather than assuming that every wake-up means the requested event has occurred.

A useful rule:

    Wake-up does not necessarily mean condition satisfied.

The task must verify the condition.

## 4.7 finish_wait()

When the waiting condition becomes true, the task calls:

    finish_wait(&my_wait_queue, &wait);

This function ensures that the wait entry is removed and that the task is returned to the running state as appropriate.

The task can then continue its normal execution.

---

# 5. Waking Up

## 5.1 wake_up()

When the event occurs, another part of the kernel wakes the waiting task.

A common interface is:

    wake_up(&my_wait_queue);

For interruptible waiters, an interface commonly used is:

    wake_up_interruptible(&my_wait_queue);

The wake-up mechanism processes entries on the wait queue and invokes their wake functions.

The scheduler's wake-up path eventually involves:

    try_to_wake_up()

## 5.2 try_to_wake_up()

The function:

    try_to_wake_up()

is an important part of the scheduler's wake-up mechanism.

Conceptually, it performs the following operations:

1. Checks whether the task is in a state that can be awakened.
2. Changes the task's state to TASK_RUNNING.
3. Activates the task if it is not already queued.
4. Places the task on an appropriate CPU runqueue.
5. Checks whether rescheduling is necessary.

The exact implementation involves locking, CPU selection, memory barriers and other synchronization mechanisms.

For a normal fair-class task, the activation path eventually makes its scheduling entity eligible for CFS scheduling again.

## 5.3 enqueue_task() and activate_task()

The scheduler uses class-specific enqueue operations through:

    enqueue_task()

The scheduler's activation path includes:

    activate_task()

For a fair-class task, the corresponding CFS enqueue logic makes the scheduling entity runnable and places it into the appropriate CFS runqueue structures.

The task can now compete for CPU time.

## 5.4 need_resched

Suppose a task wakes up and has scheduling priority over the currently running task.

The scheduler may mark the current task for rescheduling.

An important mechanism is:

    need_resched

This indicates that the current task should give up the CPU at an appropriate scheduling point.

The scheduler then selects the next task according to the scheduling-class rules.

A wake-up does not necessarily cause an immediate context switch.

The awakened task becomes runnable, and the scheduler determines when it should execute.

---

# 6. Complete Sleeping and Waking Sequence

## 6.1 Task Going to Sleep

    Running Task
         |
         v
    Event unavailable
         |
         v
    prepare_to_wait()
         |
         v
    TASK_INTERRUPTIBLE
         |
         v
    Wait queue entry added
         |
         v
    schedule()
         |
         v
    __schedule()
         |
         v
    Deactivate current task
         |
         v
    Remove from runnable queue
         |
         v
    Another task executes

## 6.2 Task Waking Up

    Required event occurs
         |
         v
    wake_up()
         |
         v
    try_to_wake_up()
         |
         v
    TASK_RUNNING
         |
         v
    activate_task()
         |
         v
    enqueue_task()
         |
         v
    CFS enqueue logic
         |
         v
    Entity becomes runnable
         |
         v
    Scheduler may request rescheduling

## 6.3 Complete Conceptual Lifecycle

    TASK_RUNNING
         |
         v
    Task requests I/O
         |
         v
    TASK_INTERRUPTIBLE
         |
         v
    Added to wait queue
         |
         v
    schedule()
         |
         v
    Task removed from runnable queue
         |
         v
    Waiting for event
         |
         v
    Event occurs
         |
         v
    wake_up()
         |
         v
    try_to_wake_up()
         |
         v
    TASK_RUNNING
         |
         v
    Re-enqueued on runqueue
         |
         v
    Scheduler selects task
         |
         v
    Task executes again

---

# 7. Important Linux 5.15 Functions

## Scheduler entry and selection

    schedule()
    __schedule()
    pick_next_task()
    pick_next_task_fair()
    pick_next_entity()
    __pick_first_entity()
    __pick_next_entity()

## CFS tree operations

    enqueue_entity()
    __enqueue_entity()
    dequeue_entity()
    __dequeue_entity()

## Red-black tree operations

    rb_link_node()
    rb_insert_color_cached()
    rb_erase_cached()
    rb_first_cached()
    rb_next()

## Sleeping and wait queues

    prepare_to_wait()
    finish_wait()
    add_wait_queue()
    remove_wait_queue()
    schedule()

## Waking

    wake_up()
    wake_up_interruptible()
    try_to_wake_up()
    activate_task()
    enqueue_task()

---

# 8. Important Kernel Source Files

    kernel/sched/core.c
    kernel/sched/fair.c
    kernel/sched/rt.c
    kernel/sched/deadline.c
    kernel/sched/idle.c
    kernel/sched/sched.h

    include/linux/sched.h
    include/linux/sched/signal.h
    include/linux/wait.h
    include/linux/rbtree.h

    kernel/sched/wait.c
    lib/rbtree.c

These are useful starting points for studying the actual Linux 5.15 implementation.

---

# 9. Learning Objectives

After completing this project, you should understand:

1. How schedule() enters the scheduler.
2. Why Linux uses multiple scheduling classes.
3. How pick_next_task() selects a scheduling class.
4. How CFS selects scheduling entities.
5. Why CFS uses a red-black tree.
6. How vruntime determines scheduling order.
7. How runnable entities are inserted and removed.
8. Why sleeping tasks are not runnable.
9. How wait queues manage sleeping tasks.
10. How wake_up() and try_to_wake_up() reactivate tasks.
11. How a woken task becomes eligible for CPU scheduling.
12. Why a wake-up does not always cause an immediate context switch.

The practical module demonstrates the red-black tree and wait-queue mechanisms separately. It does not replace the actual scheduler or modify real processes.

Suggested Source-Code Reading Order

# 1. Scheduler entry
grep -nE 'schedule\\(|__schedule\\(' kernel/sched/core.c

# 2. Scheduler class selection
grep -n 'pick_next_task' kernel/sched/core.c

# 3. CFS task selection
grep -nE 'pick_next_task_fair|pick_next_entity|__pick_first_entity|__pick_next_entity' kernel/sched/fair.c

# 4. CFS enqueue/dequeue
grep -nE '__enqueue_entity|__dequeue_entity|enqueue_entity|dequeue_entity' kernel/sched/fair.c

# 5. Task wake-up
grep -n 'try_to_wake_up' kernel/sched/core.c

# 6. Wait queue implementation
grep -nE 'prepare_to_wait|finish_wait|__wake_up' kernel/sched/wait.c


