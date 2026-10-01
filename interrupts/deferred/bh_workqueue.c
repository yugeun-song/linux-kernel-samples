// SPDX-License-Identifier: 0BSD
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
#include <linux/version.h>
#include <linux/workqueue.h>

#define SIM_IRQ_LINES 1
#define SIM_IRQ_HWIRQ 0

/* 7.2+ warns when a workqueue names neither WQ_PERCPU (6.17+) nor WQ_UNBOUND. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 17, 0)
#define WQ_PERCPU 0
#endif

static struct irq_domain *gs_sim_domain;
static struct workqueue_struct *gs_bh_wq;
static unsigned int gs_virq;

static void bh_workqueue_bottom_half(struct work_struct *work)
{
	void *buf;

	pr_info("bottom half: in_hardirq=%s in_softirq=%s in_task=%s\n", in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");

	buf = kmalloc(64, GFP_ATOMIC);
	if (!buf) {
		pr_err("GFP_ATOMIC kmalloc failed\n");
		return;
	}
	kfree(buf);
	pr_info("GFP_ATOMIC kmalloc ok; a BH work runs in softirq, so sleeping is still forbidden\n");
}

static DECLARE_WORK(gs_bottom_half, bh_workqueue_bottom_half);

static irqreturn_t bh_workqueue_top_half(int irq, void *dev_id)
{
	pr_info("top half: in_hardirq=%s in_softirq=%s in_task=%s\n", in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	pr_info("queueing the BH-workqueue bottom half\n");
	queue_work(gs_bh_wq, &gs_bottom_half);
	return IRQ_HANDLED;
}

static int __init bh_workqueue_module_init(void)
{
	int ret;

	pr_info("init: in_hardirq=%s in_softirq=%s in_task=%s\n", in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");

	gs_bh_wq = alloc_workqueue(KBUILD_MODNAME, WQ_BH | WQ_PERCPU, 0);
	if (!gs_bh_wq) {
		pr_err("alloc_workqueue failed\n");
		return -ENOMEM;
	}

	gs_sim_domain = irq_domain_create_sim(NULL, SIM_IRQ_LINES);
	if (IS_ERR(gs_sim_domain)) {
		ret = PTR_ERR(gs_sim_domain);
		pr_err("irq_domain_create_sim failed: %d\n", ret);
		goto err_destroy_wq;
	}

	gs_virq = irq_create_mapping(gs_sim_domain, SIM_IRQ_HWIRQ);
	if (!gs_virq) {
		pr_err("irq_create_mapping failed\n");
		ret = -ENODEV;
		goto err_remove_sim;
	}

	ret = request_irq(gs_virq, bh_workqueue_top_half, 0, KBUILD_MODNAME, NULL);
	if (ret) {
		pr_err("request_irq failed: %d\n", ret);
		goto err_dispose_mapping;
	}

	ret = irq_set_irqchip_state(gs_virq, IRQCHIP_STATE_PENDING, true);
	if (ret) {
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
err_destroy_wq:
	destroy_workqueue(gs_bh_wq);
	return ret;
}

static void __exit bh_workqueue_module_exit(void)
{
	pr_info("exit: in_hardirq=%s in_softirq=%s in_task=%s\n", in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	free_irq(gs_virq, NULL);
	irq_dispose_mapping(gs_virq);
	irq_domain_remove_sim(gs_sim_domain);
	destroy_workqueue(gs_bh_wq);
	pr_info("unloaded\n");
}

module_init(bh_workqueue_module_init);
module_exit(bh_workqueue_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("BH workqueue bottom half in softirq context");
MODULE_VERSION("1.0");
