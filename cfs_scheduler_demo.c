	// SPDX-License-Identifier: GPL-2.0
/*
 * cfs_scheduler_demo.c
 *
 * Linux Kernel 5.15
 *
 * Demonstrates:
 *
 * 1. CFS-style vruntime ordering
 * 2. Cached red-black tree
 * 3. Selecting the leftmost entity
 * 4. Enqueue and dequeue operations
 * 5. Wait queues
 * 6. Kernel thread sleeping
 * 7. Wake-up through wake_up_interruptible()
 *
 * This module does not modify the actual scheduler.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/rbtree.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/atomic.h>

/* -----------------------------------------
 * PART 1: CFS RED-BLACK TREE DEMONSTRATION
 * ----------------------------------------- */

/*
 * Simplified scheduling entity.
 *
 * Actual Linux CFS uses struct sched_entity.
 */
struct demo_entity {
    struct rb_node run_node;
    u64 vruntime;
    const char *name;
};

/*
 * Cached red-black tree.
 */
static struct rb_root_cached demo_root = RB_ROOT_CACHED;

/*
 * Demonstration entities.
 */
static struct demo_entity entities[] = {
    { .vruntime = 20, .name = "Process A" },
    { .vruntime = 5,  .name = "Process B" },
    { .vruntime = 15, .name = "Process C" },
    { .vruntime = 10, .name = "Process D" },
};

/*
 * Insert an entity according to vruntime.
 */
static void demo_enqueue(struct demo_entity *entity)
{
    struct rb_node **link;
    struct rb_node *parent = NULL;
    struct demo_entity *entry;
    bool leftmost = true;

    link = &demo_root.rb_root.rb_node;

    while (*link) {

        parent = *link;

        entry = rb_entry(parent,
                         struct demo_entity,
                         run_node);

        if (entity->vruntime < entry->vruntime) {

            link = &parent->rb_left;

        } else {

            link = &parent->rb_right;
            leftmost = false;
        }
    }

    /*
     * Link the new entity.
     */
    rb_link_node(&entity->run_node, parent, link);

    /*
     * Restore red-black properties
     * and update cached leftmost node.
     */
    rb_insert_color_cached(&entity->run_node,
                           &demo_root,
                           leftmost);

    pr_info("CFS-DEMO: Enqueued %s vruntime=%llu\n",
            entity->name,
            (unsigned long long)entity->vruntime);
}

/*
 * Select the entity with the smallest vruntime.
 */
static struct demo_entity *demo_pick_next(void)
{
    struct rb_node *left;

    left = rb_first_cached(&demo_root);

    if (!left)
        return NULL;

    return rb_entry(left,
                    struct demo_entity,
                    run_node);
}

/*
 * Remove an entity.
 */
static void demo_dequeue(struct demo_entity *entity)
{
    rb_erase_cached(&entity->run_node, &demo_root);

    pr_info("CFS-DEMO: Dequeued %s vruntime=%llu\n",
            entity->name,
            (unsigned long long)entity->vruntime);
}

/*
 * Demonstrate insertion, selection and removal.
 */
static void demo_cfs_operations(void)
{
    struct demo_entity *next;
    int i;

    pr_info("CFS-DEMO: Starting red-black tree demo\n");

    /* Insert entities. */
    for (i = 0; i < ARRAY_SIZE(entities); i++)
        demo_enqueue(&entities[i]);

    /* Select smallest vruntime. */
    next = demo_pick_next();

    if (next) {
        pr_info("CFS-DEMO: Selected %s vruntime=%llu\n",
                next->name,
                (unsigned long long)next->vruntime);

        /* Remove selected entity. */
        demo_dequeue(next);
    }

    /* Select the next smallest vruntime. */
    next = demo_pick_next();

    if (next) {
        pr_info("CFS-DEMO: Next entity %s vruntime=%llu\n",
                next->name,
                (unsigned long long)next->vruntime);
    }

    /*
     * Remove all remaining entities.
     * Always obtain the current leftmost entity
     * before deleting it.
     */
    while ((next = demo_pick_next()) != NULL)
        demo_dequeue(next);

    pr_info("CFS-DEMO: Runqueue empty\n");
}

/* -----------------------------------------
 * PART 2: WAIT QUEUE DEMONSTRATION
 * ----------------------------------------- */

/*
 * Wait queue shared by the kernel thread
 * and module initialization code.
 */
static DECLARE_WAIT_QUEUE_HEAD(demo_wait_queue);

/*
 * Event flag.
 *
 * 0 = Event not available
 * 1 = Event available
 */
static atomic_t event_ready = ATOMIC_INIT(0);

/*
 * Kernel thread pointer.
 */
static struct task_struct *demo_thread;

/*
 * Kernel thread function.
 */
static int demo_wait_thread(void *data)
{
    int ret;

    pr_info("WAIT-DEMO: Kernel thread started\n");

    pr_info("WAIT-DEMO: Waiting for event\n");

    /*
     * Sleep until:
     *
     * 1. Event becomes available, or
     * 2. Kernel thread is asked to stop.
     *
     * wait_event_interruptible() internally
     * manages the wait queue and task state.
     */
    ret = wait_event_interruptible(
        demo_wait_queue,
        atomic_read(&event_ready) ||
        kthread_should_stop()
    );

    if (kthread_should_stop())
        return 0;

    if (ret) {
        pr_info("WAIT-DEMO: Wait interrupted\n");
        return ret;
    }

    pr_info("WAIT-DEMO: Event received\n");

    pr_info("WAIT-DEMO: Kernel thread resumed\n");

    return 0;
}

/* -----------------------------------------
 * PART 3: MODULE INITIALIZATION
 * ----------------------------------------- */

static int __init cfs_demo_init(void)
{
    pr_info("CFS-DEMO: Module loaded\n");

    /*
     * Demonstrate CFS red-black tree operations.
     */
    demo_cfs_operations();

    /*
     * Start a real kernel thread.
     */
    demo_thread = kthread_run(
        demo_wait_thread,
        NULL,
        "cfs_demo_thread"
    );

    if (IS_ERR(demo_thread)) {
        int ret = PTR_ERR(demo_thread);

        pr_err("WAIT-DEMO: Failed to create thread\n");

        return ret;
    }

    /*
     * Allow the thread to reach its wait operation.
     */
    msleep(100);

    pr_info("WAIT-DEMO: Generating event\n");

    /*
     * Mark event available.
     */
    atomic_set(&event_ready, 1);

    /*
     * Wake the waiting kernel thread.
     */
    wake_up_interruptible(&demo_wait_queue);

    return 0;
}

/* -----------------------------------------
 * PART 4: MODULE CLEANUP
 * ----------------------------------------- */

static void __exit cfs_demo_exit(void)
{
    if (demo_thread)
        kthread_stop(demo_thread);

    pr_info("CFS-DEMO: Module unloaded\n");
}

module_init(cfs_demo_init);
module_exit(cfs_demo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linux Kernel Learning Project");
MODULE_DESCRIPTION(
    "Linux 5.15 CFS red-black tree and wait queue demonstration"
);
MODULE_VERSION("1.0");
