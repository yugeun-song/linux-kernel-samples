// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": %s() - " fmt, __func__

/*
 * Every core competes for the SAME interrupt. One simulated irq (a single virq)
 * is raised concurrently by a per-CPU kthread on every online core. Because it
 * is one line, genirq serializes the hardirq (IRQD_IRQ_INPROGRESS): many cores
 * push at once, but the handler still runs one at a time. The shared counter,
 * guarded by spin_lock_irqsave, stays gap-free across the hardirq and the
 * softirq (tasklet) that follows it.
 *
 * The log also shows the hardirq -> softirq chain: each hardirq stamps its
 * sequence number into pending_seq under the lock, and the softirq that drains
 * the tasklet reports which hardirq it is picking up. When raises coalesce, one
 * softirq may cover several hardirqs, so it prints the most recent seq it saw.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq_sim.h>
#include <linux/spinlock.h>
#include <linux/preempt.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/smpboot.h>

#define SIM_IRQ_LINES 1
#define SIM_IRQ_HWIRQ 0
#define RAISES_PER_CPU 10

static struct irq_domain *sim_domain;
static unsigned int virq;
static struct tasklet_struct bottom_half;

static DEFINE_SPINLOCK(counter_lock);
static unsigned long shared_counter;
static unsigned int hardirq_runs;
static unsigned int pending_seq;

static DEFINE_PER_CPU(unsigned int, raises_done);
static DEFINE_PER_CPU(struct task_struct *, raiser);

static void interrupt_competition_bottom_half(struct tasklet_struct *t)
{
	unsigned long flags, val;
	unsigned int seq;

	spin_lock_irqsave(&counter_lock, flags);
	seq = pending_seq;
	shared_counter++;
	val = shared_counter;
	spin_unlock_irqrestore(&counter_lock, flags);
	pr_info("softirq  cpu#%u picking up hardirq #%-3u -> shared_counter=%-4lu  in_softirq=%s\n",
		smp_processor_id(), seq, val, in_softirq() ? "Y" : "N");
}

static irqreturn_t interrupt_competition_top_half(int irq, void *dev_id)
{
	unsigned long flags, val;
	unsigned int n;

	spin_lock_irqsave(&counter_lock, flags);
	hardirq_runs++;
	n = hardirq_runs;
	shared_counter++;
	val = shared_counter;
	pending_seq = n;
	spin_unlock_irqrestore(&counter_lock, flags);
	pr_info("hardirq #%-3u cpu#%u -> shared_counter=%-4lu  in_hardirq=%s\n",
		n, smp_processor_id(), val, in_hardirq() ? "Y" : "N");
	tasklet_schedule(&bottom_half);
	return IRQ_HANDLED;
}

static int raise_should_run(unsigned int cpu)
{
	return this_cpu_read(raises_done) < RAISES_PER_CPU;
}

static void raise_thread_fn(unsigned int cpu)
{
	irq_set_irqchip_state(virq, IRQCHIP_STATE_PENDING, true);
	this_cpu_inc(raises_done);
	pr_info("cpu#%u raised the shared irq (%u/%u)\n", cpu,
		this_cpu_read(raises_done), RAISES_PER_CPU);
}

static struct smp_hotplug_thread raise_hotplug = {
	.store = &raiser,
	.thread_should_run = raise_should_run,
	.thread_fn = raise_thread_fn,
	.thread_comm = "shared_cnt_raise/%u",
};

static int __init interrupt_competition_init(void)
{
	int ret;

	pr_info("init: in_hardirq=%s in_softirq=%s in_task=%s\n",
		in_hardirq() ? "Y" : "N", in_softirq() ? "Y" : "N",
		in_task() ? "Y" : "N");

	tasklet_setup(&bottom_half, interrupt_competition_bottom_half);

	sim_domain = irq_domain_create_sim(NULL, SIM_IRQ_LINES);
	if (IS_ERR(sim_domain)) {
		ret = PTR_ERR(sim_domain);
		pr_err("irq_domain_create_sim failed: %d\n", ret);
		goto err_kill_tasklet;
	}

	virq = irq_create_mapping(sim_domain, SIM_IRQ_HWIRQ);
	if (!virq) {
		pr_err("irq_create_mapping failed\n");
		ret = -ENODEV;
		goto err_remove_sim;
	}

	ret = request_irq(virq, interrupt_competition_top_half, 0, KBUILD_MODNAME, NULL);
	if (ret) {
		pr_err("request_irq failed: %d\n", ret);
		goto err_dispose_mapping;
	}

	pr_info("every online CPU will raise this one irq %d times at once\n",
		RAISES_PER_CPU);
	ret = smpboot_register_percpu_thread(&raise_hotplug);
	if (ret) {
		pr_err("smpboot_register_percpu_thread failed: %d\n", ret);
		goto err_free_irq;
	}

	return 0;

err_free_irq:
	free_irq(virq, NULL);
err_dispose_mapping:
	irq_dispose_mapping(virq);
err_remove_sim:
	irq_domain_remove_sim(sim_domain);
err_kill_tasklet:
	tasklet_kill(&bottom_half);
	return ret;
}

static void __exit interrupt_competition_exit(void)
{
	smpboot_unregister_percpu_thread(&raise_hotplug);
	free_irq(virq, NULL);
	irq_dispose_mapping(virq);
	irq_domain_remove_sim(sim_domain);
	tasklet_kill(&bottom_half);
	pr_info("unloaded; hardirq_runs=%u shared_counter=%lu\n", hardirq_runs,
		shared_counter);
}

module_init(interrupt_competition_init);
module_exit(interrupt_competition_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Every CPU races to raise one shared irq; counter guarded by spin_lock_irqsave");
MODULE_VERSION("1.0");
