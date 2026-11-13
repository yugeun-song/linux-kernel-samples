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
#include <linux/version.h>
#include <linux/workqueue.h>

#define SIM_IRQ_LINES 1
#define SIM_IRQ_HWIRQ 0

/* 7.2+ warns when a workqueue names neither WQ_PERCPU (6.17+) nor WQ_UNBOUND */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 17, 0)
# define WQ_PERCPU 0
#endif

static struct irq_domain *gs_sim_domain;
static struct workqueue_struct *gs_proc_wq;
static unsigned int gs_virq;

static void run_workqueue_sample_bottom_half(struct work_struct *const mut_work)
{
	pr_info("bottom half: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");

	void *buf = kmalloc(64, GFP_KERNEL);
	if (buf == NULL) {
		pr_err("GFP_KERNEL kmalloc failed\n");
		return;
	}
	kfree(buf);
	buf = NULL;
	pr_info("GFP_KERNEL kmalloc ok; process context may sleep\n");

	msleep(10);
	pr_info("slept 10ms; sleeping is allowed here, unlike hardirq/softirq\n");
}

static irqreturn_t handle_workqueue_sample_top_half(int irq, void *const mut_dev_id_nullable)
{
	static DECLARE_WORK(s_bottom_half, run_workqueue_sample_bottom_half);

	pr_info("top half: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");
	pr_info("queueing the process-context bottom half\n");
	queue_work(gs_proc_wq, &s_bottom_half);
	return IRQ_HANDLED;
}

static int __init init_workqueue_sample_module(void)
{
	pr_info("init: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");

	gs_proc_wq = alloc_workqueue(KBUILD_MODNAME, WQ_PERCPU, 0);
	if (gs_proc_wq == NULL) {
		pr_err("alloc_workqueue failed\n");
		return -ENOMEM;
	}

	int ret;

	gs_sim_domain = irq_domain_create_sim(NULL, SIM_IRQ_LINES);
	if (IS_ERR(gs_sim_domain)) {
		ret = (int)PTR_ERR(gs_sim_domain);
		pr_err("irq_domain_create_sim failed: %d\n", ret);
		goto err_destroy_wq;
	}

	gs_virq = irq_create_mapping(gs_sim_domain, SIM_IRQ_HWIRQ);
	if (gs_virq == 0) {
		pr_err("irq_create_mapping failed\n");
		ret = -ENODEV;
		goto err_remove_sim;
	}

	ret = request_irq(gs_virq, handle_workqueue_sample_top_half, 0, KBUILD_MODNAME, NULL);
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
err_destroy_wq:
	destroy_workqueue(gs_proc_wq);
	gs_proc_wq = NULL;
	return ret;
}

static void __exit exit_workqueue_sample_module(void)
{
	pr_info("exit: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");
	free_irq(gs_virq, NULL);
	irq_dispose_mapping(gs_virq);
	irq_domain_remove_sim(gs_sim_domain);
	gs_sim_domain = NULL;
	destroy_workqueue(gs_proc_wq);
	gs_proc_wq = NULL;
	pr_info("unloaded\n");
}

module_init(init_workqueue_sample_module);
module_exit(exit_workqueue_sample_module);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Workqueue bottom half in process context");
MODULE_VERSION("1.0");
