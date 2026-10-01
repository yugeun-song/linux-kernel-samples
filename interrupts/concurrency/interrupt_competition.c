// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/*
 * Every core competes for the SAME interrupt. One simulated IRQ (a single virq)
 * is raised by a per-CPU kthread on every online core, and each kthread waits
 * for the next tick between raises, so the cores raise on the same tick. Because
 * it is one line, raises are not queued: irq_sim keeps one pending bit per line
 * and one irq_work per domain, so raises that land together merge, and genirq
 * drops one that arrives while the handler is INPROGRESS. The handler runs one
 * at a time; the shared counter, guarded by spin_lock_irqsave, stays gap-free
 * across the hardirq and the softirq (tasklet) that follows it.
 *
 * The log also shows the hardirq -> softirq chain: each hardirq stamps its
 * sequence number into gs_pending_seq under the lock, and the softirq that drains
 * the tasklet reports which hardirq it is picking up. When raises coalesce, one
 * softirq may cover several hardirqs, so it prints the most recent seq it saw.
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq_sim.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/preempt.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smpboot.h>
#include <linux/spinlock.h>

#define SIM_IRQ_LINES 1
#define SIM_IRQ_HWIRQ 0
#define RAISES_PER_CPU 10

static struct irq_domain *gs_sim_domain;
static unsigned int gs_virq;
static struct tasklet_struct gs_bottom_half;

static DEFINE_SPINLOCK(gs_counter_lock);
static unsigned long gs_shared_count;
static unsigned int gs_hardirq_run_count;
static unsigned int gs_pending_seq;

static DEFINE_PER_CPU(unsigned int, gs_raise_count);
static DEFINE_PER_CPU(struct task_struct *, gs_thread);

static void interrupt_competition_bottom_half(struct tasklet_struct *t)
{
	unsigned long flags, val;
	unsigned int seq;

	spin_lock_irqsave(&gs_counter_lock, flags);
	seq = gs_pending_seq;
	gs_shared_count++;
	val = gs_shared_count;
	spin_unlock_irqrestore(&gs_counter_lock, flags);
	pr_info("softirq  CPU#%u picking up hardirq #%-3u -> gs_shared_count=%-4lu  in_softirq=%s\n",
		smp_processor_id(), seq, val, in_softirq() ? "Y" : "N");
}

static irqreturn_t interrupt_competition_top_half(int irq, void *dev_id)
{
	unsigned long flags, val;
	unsigned int n;

	spin_lock_irqsave(&gs_counter_lock, flags);
	gs_hardirq_run_count++;
	n = gs_hardirq_run_count;
	gs_shared_count++;
	val = gs_shared_count;
	gs_pending_seq = n;
	spin_unlock_irqrestore(&gs_counter_lock, flags);
	pr_info("hardirq #%-3u CPU#%u -> gs_shared_count=%-4lu  in_hardirq=%s\n", n,
		smp_processor_id(), val, in_hardirq() ? "Y" : "N");
	tasklet_schedule(&gs_bottom_half);
	return IRQ_HANDLED;
}

static int irq_raiser_should_run(unsigned int cpu)
{
	return this_cpu_read(gs_raise_count) < RAISES_PER_CPU;
}

static void run_irq_raiser(unsigned int cpu)
{
	irq_set_irqchip_state(gs_virq, IRQCHIP_STATE_PENDING, true);
	this_cpu_inc(gs_raise_count);
	pr_info("CPU#%u raised the shared irq (%u/%u)\n", cpu, this_cpu_read(gs_raise_count),
		RAISES_PER_CPU);
	schedule_timeout_interruptible(1);
}

static struct smp_hotplug_thread gs_irq_raiser_thread = {
	.store = &gs_thread,
	.thread_should_run = irq_raiser_should_run,
	.thread_fn = run_irq_raiser,
	.thread_comm = "irq_raiser/%u",
};

static int __init interrupt_competition_module_init(void)
{
	int ret;

	pr_info("init: in_hardirq=%s in_softirq=%s in_task=%s\n", in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");

	tasklet_setup(&gs_bottom_half, interrupt_competition_bottom_half);

	gs_sim_domain = irq_domain_create_sim(NULL, SIM_IRQ_LINES);
	if (IS_ERR(gs_sim_domain)) {
		ret = PTR_ERR(gs_sim_domain);
		pr_err("irq_domain_create_sim failed: %d\n", ret);
		goto err_kill_tasklet;
	}

	gs_virq = irq_create_mapping(gs_sim_domain, SIM_IRQ_HWIRQ);
	if (!gs_virq) {
		pr_err("irq_create_mapping failed\n");
		ret = -ENODEV;
		goto err_remove_sim;
	}

	ret = request_irq(gs_virq, interrupt_competition_top_half, 0, KBUILD_MODNAME, NULL);
	if (ret) {
		pr_err("request_irq failed: %d\n", ret);
		goto err_dispose_mapping;
	}

	pr_info("every online CPU will raise this one irq %d times at once\n", RAISES_PER_CPU);
	ret = smpboot_register_percpu_thread(&gs_irq_raiser_thread);
	if (ret) {
		pr_err("smpboot_register_percpu_thread failed: %d\n", ret);
		goto err_free_irq;
	}

	pr_info("loaded\n");
	return 0;

err_free_irq:
	free_irq(gs_virq, NULL);
err_dispose_mapping:
	irq_dispose_mapping(gs_virq);
err_remove_sim:
	irq_domain_remove_sim(gs_sim_domain);
err_kill_tasklet:
	tasklet_kill(&gs_bottom_half);
	return ret;
}

static void __exit interrupt_competition_module_exit(void)
{
	pr_info("exit: in_hardirq=%s in_softirq=%s in_task=%s\n", in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	smpboot_unregister_percpu_thread(&gs_irq_raiser_thread);
	free_irq(gs_virq, NULL);
	irq_dispose_mapping(gs_virq);
	irq_domain_remove_sim(gs_sim_domain);
	tasklet_kill(&gs_bottom_half);
	pr_info("unloaded; gs_hardirq_run_count=%u gs_shared_count=%lu\n", gs_hardirq_run_count,
		gs_shared_count);
}

module_init(interrupt_competition_module_init);
module_exit(interrupt_competition_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Shared IRQ raised by every CPU at once, counter guarded by spin_lock_irqsave");
MODULE_VERSION("1.0");
