/* SPDX-License-Identifier: 0BSD */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/slab.h>

#define SIM_IRQ_LINES 1
#define SIM_IRQ_HWIRQ 0

static struct irq_domain *gs_sim_domain;
static unsigned int gs_virq;

static irqreturn_t handle_threaded_irq_top_half(int irq, void *const mut_dev_id_nullable)
{
	pr_info("top half: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");
	pr_info("waking the irq thread for the bottom half\n");
	return IRQ_WAKE_THREAD;
}

static irqreturn_t run_threaded_irq_bottom_half(int irq, void *const mut_dev_id_nullable)
{
	pr_info("bottom half: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");

	void *buf = kmalloc(64, GFP_KERNEL);
	if (buf == NULL) {
		pr_err("GFP_KERNEL kmalloc failed\n");
		return IRQ_HANDLED;
	}
	kfree(buf);
	buf = NULL;
	pr_info("GFP_KERNEL kmalloc ok; the irq thread runs in its own kthread and may sleep\n");

	msleep(10);
	pr_info("slept 10ms in the irq thread; allowed in this dedicated kthread\n");

	return IRQ_HANDLED;
}

static int __init init_threaded_irq_module(void)
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

	ret = request_threaded_irq(gs_virq, handle_threaded_irq_top_half,
				   run_threaded_irq_bottom_half, IRQF_ONESHOT, KBUILD_MODNAME,
				   NULL);
	if (ret != 0) {
		pr_err("request_threaded_irq failed: %d\n", ret);
		goto err_dispose_mapping;
	}

	ret = irq_set_irqchip_state(gs_virq, IRQCHIP_STATE_PENDING, true);
	if (ret != 0) {
		pr_err("failed to raise the simulated irq: %d\n", ret);
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
	gs_sim_domain = NULL;
	return ret;
}

static void __exit exit_threaded_irq_module(void)
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

module_init(init_threaded_irq_module);
module_exit(exit_threaded_irq_module);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Threaded IRQ bottom half in a dedicated kthread");
MODULE_VERSION("1.0");
