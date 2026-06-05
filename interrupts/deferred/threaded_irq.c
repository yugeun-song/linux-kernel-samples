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
#include <linux/delay.h>
#include <linux/preempt.h>

#define SIM_IRQ_LINES 1
#define SIM_IRQ_HWIRQ 0

static struct irq_domain *sim_domain;
static unsigned int virq;

static irqreturn_t threaded_irq_top_half(int irq, void *dev_id)
{
	pr_info("top half: in_hardirq=%s in_softirq=%s in_task=%s\n",
		in_hardirq() ? "Y" : "N", in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	pr_info("waking the irq thread for the bottom half\n");
	return IRQ_WAKE_THREAD;
}

static irqreturn_t threaded_irq_bottom_half(int irq, void *dev_id)
{
	void *buf;

	pr_info("bottom half: in_hardirq=%s in_softirq=%s in_task=%s\n",
		in_hardirq() ? "Y" : "N", in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");

	buf = kmalloc(64, GFP_KERNEL);
	if (!buf) {
		pr_err("GFP_KERNEL kmalloc failed\n");
		return IRQ_HANDLED;
	}
	kfree(buf);
	pr_info("GFP_KERNEL kmalloc ok; the irq thread runs in its own kthread and may sleep\n");

	msleep(10);
	pr_info("slept 10ms in the irq thread; allowed in this dedicated kthread\n");

	return IRQ_HANDLED;
}

static int __init threaded_irq_init(void)
{
	int ret;

	pr_info("init: in_hardirq=%s in_softirq=%s in_task=%s\n",
		in_hardirq() ? "Y" : "N", in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");

	sim_domain = irq_domain_create_sim(NULL, SIM_IRQ_LINES);
	if (IS_ERR(sim_domain)) {
		ret = PTR_ERR(sim_domain);
		pr_err("irq_domain_create_sim failed: %d\n", ret);
		return ret;
	}

	virq = irq_create_mapping(sim_domain, SIM_IRQ_HWIRQ);
	if (!virq) {
		pr_err("irq_create_mapping failed\n");
		ret = -ENODEV;
		goto err_remove_sim;
	}

	ret = request_threaded_irq(virq, threaded_irq_top_half, threaded_irq_bottom_half,
				   IRQF_ONESHOT, KBUILD_MODNAME, NULL);
	if (ret) {
		pr_err("request_threaded_irq failed: %d\n", ret);
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
	return ret;
}

static void __exit threaded_irq_exit(void)
{
	pr_info("exit: in_hardirq=%s in_softirq=%s in_task=%s\n",
		in_hardirq() ? "Y" : "N", in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	free_irq(virq, NULL);
	irq_dispose_mapping(virq);
	irq_domain_remove_sim(sim_domain);
	pr_info("unloaded\n");
}

module_init(threaded_irq_init);
module_exit(threaded_irq_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Threaded IRQ bottom half in a dedicated kthread");
MODULE_VERSION("1.0");
