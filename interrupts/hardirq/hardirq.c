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

static struct irq_domain *sim_domain;
static unsigned int virq;

static irqreturn_t hardirq_handler(int irq, void *dev_id)
{
	void *buf;

	pr_info("fired in hardirq context: in_hardirq=%u in_softirq=%u\n",
		in_hardirq() ? 1 : 0, in_softirq() ? 1 : 0);

	buf = kmalloc(64, GFP_ATOMIC);
	if (!buf) {
		pr_err("GFP_ATOMIC kmalloc failed\n");
		return IRQ_NONE;
	}
	kfree(buf);
	pr_info("GFP_ATOMIC kmalloc ok; GFP_KERNEL is forbidden here (it may sleep)\n");

	return IRQ_HANDLED;
}

static int __init hardirq_init(void)
{
	int ret;

	sim_domain = irq_domain_create_sim(NULL, 1);
	if (IS_ERR(sim_domain)) {
		ret = PTR_ERR(sim_domain);
		pr_err("irq_domain_create_sim failed: %d\n", ret);
		return ret;
	}

	virq = irq_create_mapping(sim_domain, 0);
	if (!virq) {
		pr_err("irq_create_mapping failed\n");
		ret = -ENODEV;
		goto err_remove_sim;
	}

	ret = request_irq(virq, hardirq_handler, 0, KBUILD_MODNAME, NULL);
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
	return ret;
}

static void __exit hardirq_exit(void)
{
	free_irq(virq, NULL);
	irq_dispose_mapping(virq);
	irq_domain_remove_sim(sim_domain);
	pr_info("unloaded\n");
}

module_init(hardirq_init);
module_exit(hardirq_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hardware IRQ top-half handler on a simulated irq, fired once at load");
MODULE_VERSION("1.0");
