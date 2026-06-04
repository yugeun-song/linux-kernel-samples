// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": %s() - " fmt, __func__

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/sched.h>
#include <linux/smpboot.h>
#include <linux/jiffies.h>

#define INTERVAL_MS 1000

static DEFINE_PER_CPU(long, counter);
static DEFINE_PER_CPU(struct task_struct *, thread);

static void percpu_parallel_read_and_inc(unsigned int cpu)
{
	long val = this_cpu_read(counter);

	this_cpu_inc(counter);
	pr_info("cpu%u=%ld\n", cpu, val);
}

static int percpu_parallel_should_run(unsigned int cpu)
{
	pr_info("cpu%u -> run\n", cpu);
	return 1;
}

static void percpu_parallel_worker(unsigned int cpu)
{
	percpu_parallel_read_and_inc(cpu);
	schedule_timeout_interruptible(msecs_to_jiffies(INTERVAL_MS));
}

static struct smp_hotplug_thread percpu_parallel_thread = {
	.store = &thread,
	.thread_should_run = percpu_parallel_should_run,
	.thread_fn = percpu_parallel_worker,
	.thread_comm = "percpu_parallel/%u",
};

static int __init percpu_parallel_init(void)
{
	unsigned int cpu;

	pr_info("started interval=%ums\n", INTERVAL_MS);

	for_each_possible_cpu(cpu) {
		per_cpu(counter, cpu) = (long)(cpu + 1) * 100;
	}

	return smpboot_register_percpu_thread(&percpu_parallel_thread);
}

static void __exit percpu_parallel_exit(void)
{
	smpboot_unregister_percpu_thread(&percpu_parallel_thread);
	pr_info("stopped\n");
}

module_init(percpu_parallel_init);
module_exit(percpu_parallel_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Per-CPU counters incremented lock-free by one smpboot kthread per CPU");
MODULE_VERSION("1.0");
