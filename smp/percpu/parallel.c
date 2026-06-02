// SPDX-License-Identifier: 0BSD
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/sched.h>
#include <linux/smpboot.h>
#include <linux/jiffies.h>

#define INTERVAL_MS 1000

static DEFINE_PER_CPU(long, counter);
static DEFINE_PER_CPU(struct task_struct *, thread);

static void percpu_parallel_work(unsigned int cpu)
{
	long val = this_cpu_read(counter);

	this_cpu_inc(counter);
	pr_info("percpu/parallel: cpu%u=%ld\n", cpu, val);
}

static int percpu_parallel_should_run(unsigned int cpu)
{
	return 1;
}

static void percpu_parallel_worker(unsigned int cpu)
{
	percpu_parallel_work(cpu);
	schedule_timeout_interruptible(msecs_to_jiffies(INTERVAL_MS));
}

static struct smp_hotplug_thread percpu_parallel_thread = {
	.store			= &thread,
	.thread_should_run	= percpu_parallel_should_run,
	.thread_fn		= percpu_parallel_worker,
	.thread_comm		= "parallel/%u",
};

static int __init percpu_parallel_init(void)
{
	unsigned int cpu;

	pr_info("percpu/parallel: started interval=%ums\n", INTERVAL_MS);

	for_each_possible_cpu(cpu) {
		per_cpu(counter, cpu) = (long)(cpu + 1) * 100;
	}

	return smpboot_register_percpu_thread(&percpu_parallel_thread);
}

static void __exit percpu_parallel_exit(void)
{
	smpboot_unregister_percpu_thread(&percpu_parallel_thread);
	pr_info("percpu/parallel: stopped\n");
}

module_init(percpu_parallel_init);
module_exit(percpu_parallel_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yugeun Song");
MODULE_DESCRIPTION("Per-CPU kthreads (one per CPU) each updating only its own per-CPU counter lock-free; hotplug- and suspend/resume-safe via the smpboot per-CPU thread infrastructure");
MODULE_VERSION("1.0");
