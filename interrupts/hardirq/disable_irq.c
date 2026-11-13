/* SPDX-License-Identifier: 0BSD */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq_sim.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/printk.h>

#define SIM_IRQ_LINES 1
#define SIM_IRQ_HWIRQ 0
#define DISABLED_RAISE_COUNT 5
#define RAISE_INTERVAL_MS 1000

static struct irq_domain *gs_sim_domain;
static unsigned int gs_virq;
static atomic_t gs_handler_run_count = ATOMIC_INIT(0);

static irqreturn_t handle_disable_irq_top_half(int irq, void *const mut_dev_id_nullable)
{
	pr_info("top half: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");
	pr_info("delivered by software resend from enable_irq (irq_sim has no hardware retrigger), so this runs in softirq, not hardirq\n");
	pr_info("handler ran -> IRQ_HANDLED (run #%d)\n", atomic_inc_return(&gs_handler_run_count));
	return IRQ_HANDLED;
}

static int __init init_disable_irq_module(void)
{
	pr_info("init: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");

	int ret;

	gs_sim_domain = irq_domain_create_sim(NULL, SIM_IRQ_LINES);
	if (IS_ERR(gs_sim_domain)) {
		ret = (int)PTR_ERR(gs_sim_domain);
		pr_err("irq_domain_create_sim failed: %d\n", ret);
		return ret;
	}

	gs_virq = irq_create_mapping(gs_sim_domain, SIM_IRQ_HWIRQ);
	if (gs_virq == 0) {
		pr_err("irq_create_mapping failed\n");
		ret = -ENODEV;
		goto err_remove_sim;
	}

	ret = request_irq(gs_virq, handle_disable_irq_top_half, 0, KBUILD_MODNAME, NULL);
	if (ret != 0) {
		pr_err("request_irq failed: %d\n", ret);
		goto err_dispose_mapping;
	}

	disable_irq(gs_virq);
	pr_info("irq disabled; raises now only set IRQS_PENDING\n");

	for (size_t i = 1; i <= DISABLED_RAISE_COUNT; ++i) {
		ret = irq_set_irqchip_state(gs_virq, IRQCHIP_STATE_PENDING, true);
		if (ret != 0) {
			pr_err("failed to raise the simulated irq: %d\n", ret);
			goto err_enable_irq;
		}
		msleep(RAISE_INTERVAL_MS);
		pr_info("raise %zu/%d while disabled -> handler runs so far: %d\n", i,
			DISABLED_RAISE_COUNT, atomic_read(&gs_handler_run_count));
	}

	pr_info("calling enable_irq -- the pending irq is delivered now\n");
	enable_irq(gs_virq);

	msleep(RAISE_INTERVAL_MS);
	pr_info("%d raises while disabled -> handler runs after enable_irq: %d\n",
		DISABLED_RAISE_COUNT, atomic_read(&gs_handler_run_count));

	pr_info("loaded\n");
	return 0;

err_enable_irq:
	enable_irq(gs_virq);
	free_irq(gs_virq, NULL);
err_dispose_mapping:
	irq_dispose_mapping(gs_virq);
err_remove_sim:
	irq_domain_remove_sim(gs_sim_domain);
	gs_sim_domain = NULL;
	return ret;
}

static void __exit exit_disable_irq_module(void)
{
	pr_info("exit: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");
	free_irq(gs_virq, NULL);
	irq_dispose_mapping(gs_virq);
	irq_domain_remove_sim(gs_sim_domain);
	gs_sim_domain = NULL;
	pr_info("unloaded\n");
}

module_init(init_disable_irq_module);
module_exit(exit_disable_irq_module);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Simulated IRQ held pending by disable_irq until enable_irq");
MODULE_VERSION("1.0");
