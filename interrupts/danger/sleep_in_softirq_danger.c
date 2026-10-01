// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/*
 * WARNING: this module deliberately performs an ILLEGAL kernel operation
 * for teaching purposes: it sleeps (msleep) inside softirq context (a
 * tasklet). Softirq context is atomic and must NEVER block; doing so can
 * trip a "scheduling while atomic" BUG or wedge the machine. The illegal
 * path runs ONLY when the danger_enabled debugfs knob is set to 1; with
 * danger_enabled=0 the trigger takes a SAFE mock path that does not sleep
 * and only logs that the dangerous work was skipped. Never load this on a
 * machine you care about -- use a throwaway VM.
 *
 * Why a tasklet: a module cannot register its own softirq (open_softirq is not
 * exported), so a tasklet -- which runs on TASKLET_SOFTIRQ -- is the standard
 * way for module code to reach softirq context. The file name names the hazard
 * (sleeping in softirq), not the vehicle (the tasklet) used to get there.
 *
 * Verified on kernel 7.3.0-rc3 (virtme-ng config plus GPIO_SIM for irq_sim) with
 * danger_enabled=1. Captured live with ftrace function_graph
 * (set_graph_function=softirq_danger_bottom_half, max_graph_depth=30) and dmesg. The
 * captured trace is longer than what is shown here; every part trimmed from the
 * real output is marked with "..." (deep callee internals -- BUG and WARNING
 * reporting, the scheduler -- and the setup/teardown calls around the context
 * switch).
 *
 * ftrace function_graph:
 *
 *  0)               |  softirq_danger_bottom_half [sleep_in_softirq_danger]() {
 *  0)               |    _printk() {
 *  0)               |      ...
 *  0)   6.797 us    |    }
 *  0)               |    ...
 *  0)               |    msleep() {
 *  0)               |      ...
 *  0)               |      schedule_timeout_uninterruptible() {
 *  0)               |        schedule_timeout() {
 *  0)               |          ...
 *  0)               |          schedule() {
 *  0)               |            __schedule_bug() {
 *  0)               |              ...
 *  0) # 1169.699 us |            }
 *  0)               |            ...
 *  0)               |            dequeue_task_fair() {
 *  0)               |              ...
 *  0)   3.311 us    |            }
 *  0)               |            pick_task_fair() {
 *  0)               |              ...
 *  0)   0.385 us    |            }
 *  0)               |            ...
 *  0)               |            __warn() {
 *  0)               |              ...
 *  0) ! 385.340 us  |            }
 *  0)               |            ...
 *  0)               |            finish_task_switch.isra.0() {
 *  0)               |              ...
 *  0)   2.242 us    |            }
 *  0) @ 102891.4 us |          }
 *  0)               |          ...
 *  0) @ 102895.3 us |        }
 *  0) @ 102895.5 us |      }
 *  0) @ 102896.0 us |    }
 *  0)               |    ...
 *  0) @ 102921.3 us |  }
 *
 * dmesg (unreliable "?" stack-scan frames replaced with "..."):
 *
 *  sleep_in_softirq_danger: bottom half: in_hardirq=N in_softirq=Y in_task=N
 *  sleep_in_softirq_danger: danger_enabled=1: about to sleep in softirq context -- this is illegal
 *  BUG: scheduling while atomic: sh/82/0x00000101
 *  Call Trace:
 *   <IRQ>
 *   ...
 *   __schedule_bug.cold+0x3c/0x4e
 *   ...
 *   __schedule+0xa2a/0xfb0
 *   schedule+0x2c/0xb0
 *   ...
 *   schedule_timeout+0xa1/0x120
 *   ...
 *   msleep+0x1f/0x30
 *   ...
 *   softirq_danger_bottom_half+0x87/0xa0 [sleep_in_softirq_danger]
 *   ...
 *   tasklet_action_common+0xc0/0x230
 *   tasklet_action+0x2d/0x40
 *   handle_softirqs+0xc2/0x250
 *   ...
 *   __irq_exit_rcu+0xa6/0xe0
 *   irq_exit_rcu+0x12/0x20
 *   sysvec_irq_work+0x81/0x90
 *   </IRQ>
 *   <TASK>
 *   asm_sysvec_irq_work+0x1f/0x30
 *   ...
 *   irq_set_irqchip_state+0xb0/0x120
 *   trigger_write+0x27/0x40 [sleep_in_softirq_danger]
 *   ...
 *   full_proxy_write+0x68/0xb0
 *   vfs_write+0xd6/0x580
 *   ...
 *   ksys_write+0x7a/0x100
 *   ...
 *   do_syscall_64+0xcf/0x4b0
 *   entry_SYSCALL_64_after_hwframe+0x77/0x7f
 *  ...
 *  WARNING: arch/x86/kernel/process_64.c:616 at __switch_to+0x43b/0x4e0, CPU#0: sh/82
 *  ...
 *  sleep_in_softirq_danger: returned from the illegal sleep; the system may now be unstable
 *  softirq: huh, entered softirq 6 TASKLET 00000000455cbd0d with preempt_count 00000100, exited with 00000000?
 *
 * => same as the hardirq case but reached via irq_exit_rcu -> handle_softirqs
 *    -> tasklet_action -> softirq_danger_bottom_half (softirq, preempt_count
 *    0x00000101): __schedule_bug warns, schedule() still context-switches on the
 *    hardirq stack (the __switch_to WARNING), and the CPU sleeps ~103 ms
 *    (@ 102921 us). On return the softirq core reports the preempt_count it lost
 *    ("huh, entered softirq 6 TASKLET ..."); the system is left unstable.
 *
 * debugfs here is NOT part of the demo proper; it is the safety/test gate that
 * keeps the illegal path from ever firing by accident.
 */

#include <linux/compiler.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq_sim.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/printk.h>
#include <linux/types.h>

#define SIM_IRQ_LINES 1
#define SIM_IRQ_HWIRQ 0

static struct irq_domain *gs_sim_domain;
static struct dentry *gs_debug_dir;
static unsigned int gs_virq;
static bool gs_debugfs_danger_enabled;
static struct tasklet_struct gs_danger_bh;

static void softirq_danger_bottom_half(struct tasklet_struct *t)
{
	pr_info("bottom half: in_hardirq=%s in_softirq=%s in_task=%s\n", in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	if (!READ_ONCE(gs_debugfs_danger_enabled)) {
		pr_warn("danger_enabled=0: skipping the illegal in-softirq sleep (safe mock, no-op)\n");
		return;
	}

	/*
	 * BUG: msleep() in softirq context sleeps while atomic. This is
	 * strictly illegal and may BUG or wedge the kernel. It exists solely
	 * to demonstrate what must NEVER be done in a tasklet/softirq.
	 */
	pr_warn("danger_enabled=1: about to sleep in softirq context -- this is illegal\n");
	msleep(100);
	pr_warn("returned from the illegal sleep; the system may now be unstable\n");
}

static irqreturn_t softirq_danger_top_half(int irq, void *dev_id)
{
	pr_info("top half: in_hardirq=%s in_softirq=%s in_task=%s\n", in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	tasklet_schedule(&gs_danger_bh);
	return IRQ_HANDLED;
}

static ssize_t trigger_write(struct file *file, const char __user *ubuf, size_t len, loff_t *ppos)
{
	int ret;

	ret = irq_set_irqchip_state(gs_virq, IRQCHIP_STATE_PENDING, true);
	if (ret) {
		pr_err("failed to raise the simulated irq: %d\n", ret);
		return ret;
	}
	return len;
}

static const struct file_operations gs_trigger_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = trigger_write,
};

static int __init sleep_in_softirq_danger_module_init(void)
{
	int ret;

	pr_info("init: in_hardirq=%s in_softirq=%s in_task=%s\n", in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");

	tasklet_setup(&gs_danger_bh, softirq_danger_bottom_half);

	gs_sim_domain = irq_domain_create_sim(NULL, SIM_IRQ_LINES);
	if (IS_ERR(gs_sim_domain)) {
		ret = PTR_ERR(gs_sim_domain);
		pr_err("irq_domain_create_sim failed: %d\n", ret);
		goto err_kill_tasklet;
	}

	gs_virq = irq_create_mapping(gs_sim_domain, SIM_IRQ_HWIRQ);
	if (!gs_virq) {
		pr_err("irq_create_mapping failed\n");
		ret = -ENODEV;
		goto err_remove_sim;
	}

	ret = request_irq(gs_virq, softirq_danger_top_half, 0, KBUILD_MODNAME, NULL);
	if (ret) {
		pr_err("request_irq failed: %d\n", ret);
		goto err_dispose_mapping;
	}

	/*
	 * debugfs is this module's only interface; without it there is no way
	 * to arm or fire anything, so fail the load cleanly instead of leaving
	 * an inert module behind.
	 */
	gs_debug_dir = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (IS_ERR(gs_debug_dir)) {
		ret = PTR_ERR(gs_debug_dir);
		pr_err("debugfs unavailable (%d); cannot create danger controls\n", ret);
		goto err_free_irq;
	}
	debugfs_create_bool("danger_enabled", 0600, gs_debug_dir, &gs_debugfs_danger_enabled);
	debugfs_create_file("trigger", 0200, gs_debug_dir, NULL, &gs_trigger_fops);

	pr_info("loaded; set .../%s/danger_enabled to 1 then write .../trigger for the illegal path; trigger alone is a safe mock\n",
		KBUILD_MODNAME);
	return 0;

err_free_irq:
	free_irq(gs_virq, NULL);
err_dispose_mapping:
	irq_dispose_mapping(gs_virq);
err_remove_sim:
	irq_domain_remove_sim(gs_sim_domain);
err_kill_tasklet:
	tasklet_kill(&gs_danger_bh);
	return ret;
}

static void __exit sleep_in_softirq_danger_module_exit(void)
{
	pr_info("exit: in_hardirq=%s in_softirq=%s in_task=%s\n", in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	debugfs_remove(gs_debug_dir);
	free_irq(gs_virq, NULL);
	irq_dispose_mapping(gs_virq);
	irq_domain_remove_sim(gs_sim_domain);
	tasklet_kill(&gs_danger_bh);
	pr_info("unloaded\n");
}

module_init(sleep_in_softirq_danger_module_init);
module_exit(sleep_in_softirq_danger_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Illegal sleep in softirq context (debugfs-gated)");
MODULE_VERSION("1.0");
