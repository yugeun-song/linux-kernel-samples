// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": %s() - " fmt, __func__

/*
 * A shared counter bumped by a hardirq top half and a softirq bottom half,
 * guarded by spin_lock_irqsave(). init raises the SAME simulated irq in two ways
 * to contrast how genirq handles a flood on one line:
 *
 *   phase 1  raise FIRE_SERIAL times in a row from process context. Each raise
 *            is delivered right away, so the hardirq runs FIRE_SERIAL times,
 *            serialized (#1..#N) -- genirq processes one at a time.
 *   phase 2  raise FIRE_PER_CPU times on EVERY cpu at once via on_each_cpu, i.e.
 *            from IPI/atomic context. IPIs run with local IRQs off, so PENDING
 *            set over and over COALESCES: the whole flood (cpus x FIRE_PER_CPU
 *            raises) collapses to just a handful of hardirqs. The exact count is
 *            timing-dependent, but "many raises -> far fewer deliveries" is a
 *            kernel-wide property (an edge irq re-asserted while pending merges),
 *            not a quirk of this machine.
 *
 * Either way the lock keeps shared_counter gap-free across the hardirq/softirq
 * interleaving (cases 1 and 3). Real per-CPU parallelism (case 2) is shown by
 * the tcp sample, where packets hit the hook on many CPUs at once.
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
#include <linux/cpumask.h>

#define SIM_IRQ_LINES 1
#define SIM_IRQ_HWIRQ 0
#define FIRE_SERIAL 10
#define FIRE_PER_CPU 10

static struct irq_domain *sim_domain;
static unsigned int virq;
static struct tasklet_struct bottom_half;

static DEFINE_SPINLOCK(counter_lock);
static unsigned long shared_counter;
static unsigned int hardirq_runs;

static void shared_counter_bottom_half(struct tasklet_struct *t)
{
	unsigned long flags, val;

	spin_lock_irqsave(&counter_lock, flags);
	shared_counter++;
	val = shared_counter;
	spin_unlock_irqrestore(&counter_lock, flags);
	pr_info("softirq        cpu#%u -> shared_counter=%-4lu  in_hardirq=%s in_softirq=%s in_task=%s\n",
		smp_processor_id(), val, in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
}

static irqreturn_t shared_counter_top_half(int irq, void *dev_id)
{
	unsigned long flags, val;
	unsigned int n;

	spin_lock_irqsave(&counter_lock, flags);
	hardirq_runs++;
	n = hardirq_runs;
	shared_counter++;
	val = shared_counter;
	spin_unlock_irqrestore(&counter_lock, flags);
	pr_info("hardirq #%-3u cpu#%u -> shared_counter=%-4lu  in_hardirq=%s in_softirq=%s in_task=%s\n",
		n, smp_processor_id(), val, in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	tasklet_schedule(&bottom_half);
	return IRQ_HANDLED;
}

static void raise_local(void *unused)
{
	int i;

	for (i = 0; i < FIRE_PER_CPU; i++)
		irq_set_irqchip_state(virq, IRQCHIP_STATE_PENDING, true);
}

static int __init shared_counter_init(void)
{
	int ret, i;

	pr_info("init: in_hardirq=%s in_softirq=%s in_task=%s\n",
		in_hardirq() ? "Y" : "N", in_softirq() ? "Y" : "N",
		in_task() ? "Y" : "N");

	tasklet_setup(&bottom_half, shared_counter_bottom_half);

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

	ret = request_irq(virq, shared_counter_top_half, 0, KBUILD_MODNAME, NULL);
	if (ret) {
		pr_err("request_irq failed: %d\n", ret);
		goto err_dispose_mapping;
	}

	pr_info("phase 1: raising %d times in a row (process context, expect serialized)\n",
		FIRE_SERIAL);
	for (i = 0; i < FIRE_SERIAL; i++) {
		ret = irq_set_irqchip_state(virq, IRQCHIP_STATE_PENDING, true);
		if (ret) {
			pr_err("failed to raise the simulated irq: %d\n", ret);
			goto err_free_irq;
		}
	}

	pr_info("phase 2: raising %d times on each of %u CPUs at once (IPI, expect coalesced)\n",
		FIRE_PER_CPU, num_online_cpus());
	on_each_cpu(raise_local, NULL, 1);

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

static void __exit shared_counter_exit(void)
{
	pr_info("exit: in_hardirq=%s in_softirq=%s in_task=%s\n",
		in_hardirq() ? "Y" : "N", in_softirq() ? "Y" : "N",
		in_task() ? "Y" : "N");
	free_irq(virq, NULL);
	irq_dispose_mapping(virq);
	irq_domain_remove_sim(sim_domain);
	tasklet_kill(&bottom_half);
	pr_info("unloaded; hardirq_runs=%u shared_counter=%lu\n", hardirq_runs,
		shared_counter);
}

module_init(shared_counter_init);
module_exit(shared_counter_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Shared counter guarded by spin_lock_irqsave across hardirq and softirq");
MODULE_VERSION("1.0");
