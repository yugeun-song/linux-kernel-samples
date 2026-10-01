// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/smpboot.h>

#define INTERVAL_MS 1000

static DEFINE_PER_CPU(long, gs_count);
static DEFINE_PER_CPU(struct task_struct *, gs_thread);

static void read_and_inc_count_percpu(unsigned int cpu)
{
	long val = this_cpu_read(gs_count);

	this_cpu_inc(gs_count);
	pr_info("CPU#%u=%ld\n", cpu, val);
}

static int percpu_parallel_should_run(unsigned int cpu)
{
	pr_info("CPU#%u -> run\n", cpu);
	return 1;
}

static void run_percpu_parallel(unsigned int cpu)
{
	read_and_inc_count_percpu(cpu);
	schedule_timeout_interruptible(msecs_to_jiffies(INTERVAL_MS));
}

static struct smp_hotplug_thread gs_percpu_parallel_thread = {
	.store = &gs_thread,
	.thread_should_run = percpu_parallel_should_run,
	.thread_fn = run_percpu_parallel,
	.thread_comm = "percpu_parallel/%u",
};

static int __init percpu_parallel_module_init(void)
{
	unsigned int cpu;
	int ret;

	for_each_possible_cpu(cpu)
		per_cpu(gs_count, cpu) = (long)(cpu + 1) * 100;

	ret = smpboot_register_percpu_thread(&gs_percpu_parallel_thread);
	if (!ret)
		pr_info("loaded; interval=%ums\n", INTERVAL_MS);
	return ret;
}

static void __exit percpu_parallel_module_exit(void)
{
	smpboot_unregister_percpu_thread(&gs_percpu_parallel_thread);
	pr_info("unloaded\n");
}

module_init(percpu_parallel_module_init);
module_exit(percpu_parallel_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Per-CPU counters via one smpboot kthread per CPU");
MODULE_VERSION("1.0");
