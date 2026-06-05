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
#include <linux/workqueue.h>

#define SIM_IRQ_LINES 1
#define SIM_IRQ_HWIRQ 0

static struct irq_domain *sim_domain;
static struct workqueue_struct *bh_wq;
static unsigned int virq;

static void bh_workqueue_bottom_half(struct work_struct *work)
{
	void *buf;

	pr_info("bottom half: in_hardirq=%s in_softirq=%s in_task=%s\n",
		in_hardirq() ? "Y" : "N", in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");

	buf = kmalloc(64, GFP_ATOMIC);
	if (!buf) {
		pr_err("GFP_ATOMIC kmalloc failed\n");
		return;
	}
	kfree(buf);
	pr_info("GFP_ATOMIC kmalloc ok; a BH work runs in softirq, so sleeping is still forbidden\n");
}

static DECLARE_WORK(bottom_half, bh_workqueue_bottom_half);

static irqreturn_t bh_workqueue_top_half(int irq, void *dev_id)
{
	pr_info("top half: in_hardirq=%s in_softirq=%s in_task=%s\n",
		in_hardirq() ? "Y" : "N", in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	pr_info("queueing the BH-workqueue bottom half\n");
	queue_work(bh_wq, &bottom_half);
	return IRQ_HANDLED;
}

static int __init bh_workqueue_init(void)
{
	int ret;

	pr_info("init: in_hardirq=%s in_softirq=%s in_task=%s\n",
		in_hardirq() ? "Y" : "N", in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");

	bh_wq = alloc_workqueue(KBUILD_MODNAME, WQ_BH, 0);
	if (!bh_wq) {
		pr_err("alloc_workqueue(WQ_BH) failed\n");
		return -ENOMEM;
	}

	sim_domain = irq_domain_create_sim(NULL, SIM_IRQ_LINES);
	if (IS_ERR(sim_domain)) {
		ret = PTR_ERR(sim_domain);
		pr_err("irq_domain_create_sim failed: %d\n", ret);
		goto err_destroy_wq;
	}

	virq = irq_create_mapping(sim_domain, SIM_IRQ_HWIRQ);
	if (!virq) {
		pr_err("irq_create_mapping failed\n");
		ret = -ENODEV;
		goto err_remove_sim;
	}

	ret = request_irq(virq, bh_workqueue_top_half, 0, KBUILD_MODNAME, NULL);
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
err_destroy_wq:
	destroy_workqueue(bh_wq);
	return ret;
}

static void __exit bh_workqueue_exit(void)
{
	pr_info("exit: in_hardirq=%s in_softirq=%s in_task=%s\n",
		in_hardirq() ? "Y" : "N", in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	free_irq(virq, NULL);
	irq_dispose_mapping(virq);
	irq_domain_remove_sim(sim_domain);
	destroy_workqueue(bh_wq);
	pr_info("unloaded\n");
}

module_init(bh_workqueue_init);
module_exit(bh_workqueue_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("BH workqueue bottom half in softirq context");
MODULE_VERSION("1.0");
