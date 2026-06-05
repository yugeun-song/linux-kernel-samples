// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": %s() - " fmt, __func__

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq_sim.h>
#include <linux/delay.h>
#include <linux/preempt.h>

#define SIM_IRQ_LINES 1
#define SIM_IRQ_HWIRQ 0

static struct irq_domain *sim_domain;
static unsigned int virq;

static irqreturn_t disable_irq_top_half(int irq, void *dev_id)
{
	pr_info("top half: in_hardirq=%s in_softirq=%s in_task=%s\n",
		in_hardirq() ? "Y" : "N", in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	pr_info("handler ran -> IRQ_HANDLED\n");
	return IRQ_HANDLED;
}

static int __init disable_irq_init(void)
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

	ret = request_irq(virq, disable_irq_top_half, 0, KBUILD_MODNAME, NULL);
	if (ret) {
		pr_err("request_irq failed: %d\n", ret);
		goto err_dispose_mapping;
	}

	disable_irq(virq);
	pr_info("irq disabled (line masked)\n");

	ret = irq_set_irqchip_state(virq, IRQCHIP_STATE_PENDING, true);
	if (ret) {
		pr_err("failed to raise the simulated irq: %d\n", ret);
		goto err_enable_irq;
	}
	pr_info("irq raised while masked -- the handler stays blocked (no IRQ_HANDLED line yet)\n");

	msleep(1000);
	pr_info("still masked after 1s -- the irq is held pending, the handler has not run\n");

	enable_irq(virq);
	pr_info("irq enabled (unmasked) -- the masked irq is delivered now, so the handler runs next\n");

	return 0;

err_enable_irq:
	enable_irq(virq);
	free_irq(virq, NULL);
err_dispose_mapping:
	irq_dispose_mapping(virq);
err_remove_sim:
	irq_domain_remove_sim(sim_domain);
	return ret;
}

static void __exit disable_irq_exit(void)
{
	pr_info("exit: in_hardirq=%s in_softirq=%s in_task=%s\n",
		in_hardirq() ? "Y" : "N", in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");

	free_irq(virq, NULL);
	irq_dispose_mapping(virq);
	irq_domain_remove_sim(sim_domain);
	pr_info("unloaded\n");
}

module_init(disable_irq_init);
module_exit(disable_irq_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Masking a simulated irq with disable_irq/enable_irq");
MODULE_VERSION("1.0");
