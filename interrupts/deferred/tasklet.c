// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": %s() - " fmt, __func__

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq_sim.h>
#include <linux/slab.h>
#include <linux/preempt.h>

#define SIM_IRQ_LINES 1
#define SIM_IRQ_HWIRQ 0

static struct irq_domain *sim_domain;
static unsigned int virq;
static struct tasklet_struct bottom_half;

static void tasklet_bottom_half(struct tasklet_struct *t)
{
	void *buf;

	pr_info("bottom half in softirq context: in_hardirq=%u in_softirq=%u\n",
		in_hardirq() ? 1 : 0, in_softirq() ? 1 : 0);

	buf = kmalloc(64, GFP_ATOMIC);
	if (!buf) {
		pr_err("GFP_ATOMIC kmalloc failed\n");
		return;
	}
	kfree(buf);
	pr_info("GFP_ATOMIC kmalloc ok; sleeping (GFP_KERNEL/msleep) is still forbidden in softirq\n");
}

static irqreturn_t tasklet_top_half(int irq, void *dev_id)
{
	pr_info("top half (hardirq); scheduling the tasklet bottom half\n");
	tasklet_schedule(&bottom_half);
	return IRQ_HANDLED;
}

static int __init tasklet_sample_init(void)
{
	int ret;

	tasklet_setup(&bottom_half, tasklet_bottom_half);

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

	ret = request_irq(virq, tasklet_top_half, 0, KBUILD_MODNAME, NULL);
	if (ret) {
		pr_err("request_irq failed: %d\n", ret);
		goto err_dispose_mapping;
	}

	ret = irq_set_irqchip_state(virq, IRQCHIP_STATE_PENDING, true);
	if (ret) {
		pr_err("failed to raise the simulated irq: %d\n", ret);
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

static void __exit tasklet_sample_exit(void)
{
	free_irq(virq, NULL);
	irq_dispose_mapping(virq);
	irq_domain_remove_sim(sim_domain);
	tasklet_kill(&bottom_half);
	pr_info("unloaded\n");
}

module_init(tasklet_sample_init);
module_exit(tasklet_sample_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Tasklet bottom half (softirq context) deferred from a hardirq top half, fired once at load");
MODULE_VERSION("1.0");
