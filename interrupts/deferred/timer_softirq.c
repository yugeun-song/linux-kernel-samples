/* SPDX-License-Identifier: 0BSD */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/printk.h>
#include <linux/timer.h>

#define INTERVAL_MS 1000

static struct timer_list gs_tick_timer;
static size_t gs_tick_count;

static void run_timer_softirq_bottom_half(struct timer_list *const mut_timer)
{
	pr_info("bottom half: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");
	++gs_tick_count;
	pr_info("tick=%zu\n", gs_tick_count);
	mod_timer(mut_timer, jiffies + msecs_to_jiffies(INTERVAL_MS));
}

static int __init init_timer_softirq_module(void)
{
	pr_info("init: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");

	timer_setup(&gs_tick_timer, run_timer_softirq_bottom_half, 0);
	mod_timer(&gs_tick_timer, jiffies + msecs_to_jiffies(INTERVAL_MS));
	pr_info("loaded; interval=%ums; the top half is the kernel timer-tick interrupt, the bottom half runs in TIMER_SOFTIRQ\n",
		INTERVAL_MS);
	return 0;
}

static void __exit exit_timer_softirq_module(void)
{
	pr_info("exit: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");
	timer_shutdown_sync(&gs_tick_timer);
	pr_info("unloaded; gs_tick_count=%zu\n", gs_tick_count);
}

module_init(init_timer_softirq_module);
module_exit(exit_timer_softirq_module);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Timer bottom half in TIMER_SOFTIRQ context");
MODULE_VERSION("1.0");
