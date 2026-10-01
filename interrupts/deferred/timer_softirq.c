// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/printk.h>
#include <linux/timer.h>

#define INTERVAL_MS 1000

static struct timer_list gs_tick_timer;
static unsigned long gs_tick_count;

static void timer_softirq_bottom_half(struct timer_list *t)
{
	pr_info("bottom half: in_hardirq=%s in_softirq=%s in_task=%s\n", in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	gs_tick_count++;
	pr_info("tick=%lu\n", gs_tick_count);
	mod_timer(t, jiffies + msecs_to_jiffies(INTERVAL_MS));
}

static int __init timer_softirq_module_init(void)
{
	pr_info("init: in_hardirq=%s in_softirq=%s in_task=%s\n", in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");

	timer_setup(&gs_tick_timer, timer_softirq_bottom_half, 0);
	mod_timer(&gs_tick_timer, jiffies + msecs_to_jiffies(INTERVAL_MS));
	pr_info("loaded; interval=%ums; the top half is the kernel timer-tick interrupt, the bottom half runs in TIMER_SOFTIRQ\n",
		INTERVAL_MS);
	return 0;
}

static void __exit timer_softirq_module_exit(void)
{
	pr_info("exit: in_hardirq=%s in_softirq=%s in_task=%s\n", in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	timer_shutdown_sync(&gs_tick_timer);
	pr_info("unloaded; gs_tick_count=%lu\n", gs_tick_count);
}

module_init(timer_softirq_module_init);
module_exit(timer_softirq_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Timer bottom half in TIMER_SOFTIRQ context");
MODULE_VERSION("1.0");
