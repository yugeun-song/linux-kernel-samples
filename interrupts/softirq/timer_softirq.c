// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": %s() - " fmt, __func__

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/preempt.h>

#define INTERVAL_MS 1000

static struct timer_list tick_timer;
static unsigned long ticks;

static void timer_softirq_bottom_half(struct timer_list *t)
{
	ticks++;
	pr_info("tick=%lu in_hardirq=%u in_softirq=%u in_task=%u\n",
		ticks, in_hardirq() ? 1 : 0, in_softirq() ? 1 : 0,
		in_task() ? 1 : 0);
	mod_timer(t, jiffies + msecs_to_jiffies(INTERVAL_MS));
}

static int __init timer_softirq_init(void)
{
	timer_setup(&tick_timer, timer_softirq_bottom_half, 0);
	mod_timer(&tick_timer, jiffies + msecs_to_jiffies(INTERVAL_MS));
	pr_info("started interval=%ums; the top half is the kernel timer-tick interrupt, the bottom half runs in TIMER_SOFTIRQ\n",
		INTERVAL_MS);
	return 0;
}

static void __exit timer_softirq_exit(void)
{
	timer_shutdown_sync(&tick_timer);
	pr_info("stopped after %lu ticks\n", ticks);
}

module_init(timer_softirq_init);
module_exit(timer_softirq_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Timer bottom half in softirq context (TIMER_SOFTIRQ), driven by the kernel timer tick present on every machine");
MODULE_VERSION("1.0");
