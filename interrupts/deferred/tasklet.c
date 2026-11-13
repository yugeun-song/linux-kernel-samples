/* SPDX-License-Identifier: 0BSD */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
static struct tasklet_struct gs_bottom_half;

static void run_tasklet_bottom_half(struct tasklet_struct *const mut_tasklet)
{
	pr_info("bottom half: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");

	void *buf = kmalloc(64, GFP_ATOMIC);
	if (buf == NULL) {
		pr_err("GFP_ATOMIC kmalloc failed\n");
		return;
	}
	kfree(buf);
	buf = NULL;
	pr_info("GFP_ATOMIC kmalloc ok; sleeping (GFP_KERNEL/msleep) is still forbidden in softirq\n");
}

static irqreturn_t handle_tasklet_top_half(int irq, void *const mut_dev_id_nullable)
{
	pr_info("top half: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");
	pr_info("scheduling the tasklet bottom half\n");
	tasklet_schedule(&gs_bottom_half);
	return IRQ_HANDLED;
}

static int __init init_tasklet_module(void)
{
	pr_info("init: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");

	tasklet_setup(&gs_bottom_half, run_tasklet_bottom_half);

	int ret;

	gs_sim_domain = irq_domain_create_sim(NULL, SIM_IRQ_LINES);
	if (IS_ERR(gs_sim_domain)) {
		ret = (int)PTR_ERR(gs_sim_domain);
		pr_err("irq_domain_create_sim failed: %d\n", ret);
		goto err_kill_tasklet;
	}

	gs_virq = irq_create_mapping(gs_sim_domain, SIM_IRQ_HWIRQ);
	if (gs_virq == 0) {
		pr_err("irq_create_mapping failed\n");
		ret = -ENODEV;
		goto err_remove_sim;
	}

	ret = request_irq(gs_virq, handle_tasklet_top_half, 0, KBUILD_MODNAME, NULL);
	if (ret != 0) {
		pr_err("request_irq failed: %d\n", ret);
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
err_kill_tasklet:
	tasklet_kill(&gs_bottom_half);
	return ret;
}

static void __exit exit_tasklet_module(void)
{
	pr_info("exit: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");
	free_irq(gs_virq, NULL);
	irq_dispose_mapping(gs_virq);
	irq_domain_remove_sim(gs_sim_domain);
	gs_sim_domain = NULL;
	tasklet_kill(&gs_bottom_half);
	pr_info("unloaded\n");
}

module_init(init_tasklet_module);
module_exit(exit_tasklet_module);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Tasklet bottom half in softirq context");
MODULE_VERSION("1.0");
