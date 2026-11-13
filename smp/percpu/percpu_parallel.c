/* SPDX-License-Identifier: 0BSD */
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

static DEFINE_PER_CPU(size_t, gs_count);
static DEFINE_PER_CPU(struct task_struct *, gs_thread);

static void read_and_inc_count_percpu(unsigned int cpu)
{
	size_t val = this_cpu_read(gs_count);

	this_cpu_inc(gs_count);
	pr_info("CPU#%u=%zu\n", cpu, val);
}

static int should_run_percpu_parallel(unsigned int cpu)
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
	.thread_should_run = should_run_percpu_parallel,
	.thread_fn = run_percpu_parallel,
	.thread_comm = "percpu_parallel/%u"
};

static int __init init_percpu_parallel_module(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		per_cpu(gs_count, cpu) = (size_t)(cpu + 1) * 100;
	}

	int ret = smpboot_register_percpu_thread(&gs_percpu_parallel_thread);
	if (ret == 0) {
		pr_info("loaded; interval=%ums\n", INTERVAL_MS);
	}
	return ret;
}

static void __exit exit_percpu_parallel_module(void)
{
	smpboot_unregister_percpu_thread(&gs_percpu_parallel_thread);
	pr_info("unloaded\n");
}

module_init(init_percpu_parallel_module);
module_exit(exit_percpu_parallel_module);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Per-CPU counters via one smpboot kthread per CPU");
MODULE_VERSION("1.0");
