// SPDX-License-Identifier: 0BSD

/*
 * DANGER -- this module deliberately performs an ILLEGAL kernel operation
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
 * Verified on kernel 7.0.11 with danger_enabled=1. Captured live with ftrace
 * function_graph (set_graph_function=danger_tasklet, max_graph_depth=30) and
 * dmesg. The captured trace is longer than what is shown here; every part
 * trimmed from the real output is marked with "..." (deep callee internals --
 * BUG reporting, CFS scheduler -- and the setup/teardown calls around the
 * context switch).
 *
 * ftrace function_graph:
 *
 *  6)               |  danger_tasklet [sleep_in_softirq_danger]() {
 *  6)               |    msleep() {
 *  6)               |      ...
 *  6)               |      schedule_timeout_uninterruptible() {
 *  6)               |        schedule_timeout() {
 *  6)               |          ...
 *  6)               |          schedule() {
 *  6)               |            __schedule_bug() {
 *  6)               |              ...
 *  6) ! 202.143 us  |            }
 *  6)               |            ...
 *  6)               |            dequeue_task_fair() {
 *  6)               |              ...
 *  6)   6.081 us    |            }
 *  6)               |            pick_next_task_fair() {
 *  6)               |              ...
 *  6)   0.495 us    |            }
 *  6)               |            ...
 *  6)               |            finish_task_switch.isra.0() {
 *  6)               |              ...
 *  6)   2.849 us    |            }
 *  6) @ 100678.1 us |          }
 *  6)               |          ...
 *  6) @ 100681.9 us |        }
 *  6) @ 100682.2 us |      }
 *  6) @ 100682.8 us |    }
 *  6) @ 100701.8 us |  }
 *
 * dmesg (unreliable "?" stack-scan frames replaced with "..."):
 *
 *  sleep_in_softirq_danger: danger_tasklet() - danger_enabled=1: about to sleep in softirq context -- this is ILLEGAL
 *  BUG: scheduling while atomic: bash/152/0x00000102
 *  Call Trace:
 *   <IRQ>
 *   ...
 *   dump_stack_lvl+0x5d/0x80
 *   __schedule_bug.cold+0x42/0x4e
 *   ...
 *   __schedule+0x113c/0x1720
 *   ...
 *   schedule+0x27/0xd0
 *   ...
 *   schedule_timeout+0xa3/0x120
 *   ...
 *   msleep+0x1b/0x30
 *   ...
 *   danger_tasklet+0x3b/0x50 [sleep_in_softirq_danger 9a21c3e2794ca438e833bca03c6f6d5e6fc1c160]
 *   ...
 *   tasklet_action_common+0xe4/0x2b0
 *   handle_softirqs+0xe8/0x2c0
 *   __irq_exit_rcu+0xc9/0xf0
 *   sysvec_irq_work+0x71/0x90
 *   </IRQ>
 *   <TASK>
 *   asm_sysvec_irq_work+0x1a/0x20
 *   __irq_put_desc_unlock+0x1c/0x50
 *   irq_set_irqchip_state+0xb2/0x120
 *   trigger_write+0x23/0x40 [sleep_in_softirq_danger 9a21c3e2794ca438e833bca03c6f6d5e6fc1c160]
 *   full_proxy_write+0x6f/0xc0
 *   vfs_write+0xe6/0x570
 *   ...
 *   ksys_write+0x7b/0x110
 *   do_syscall_64+0x119/0x1640
 *   ...
 *   entry_SYSCALL_64_after_hwframe+0x76/0x7e
 *
 * => same as the hardirq case but reached via __irq_exit_rcu -> handle_softirqs
 *    -> tasklet_action_common -> danger_tasklet (softirq, preempt_count
 *    0x00000102): __schedule_bug warns, then schedule() really context-switches
 *    and the CPU actually sleeps ~100 ms (@ 100701 us) -- system left unstable.
 *
 * debugfs here is NOT part of the demo proper; it is the safety/test gate that
 * keeps the illegal path from ever firing by accident.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": %s() - " fmt, __func__

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq_sim.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/preempt.h>

static struct irq_domain *sim_domain;
static struct dentry *debug_dir;
static unsigned int virq;
static bool debugfs_danger_enabled;
static struct tasklet_struct danger_bh;

static void danger_tasklet(struct tasklet_struct *t)
{
	if (!READ_ONCE(debugfs_danger_enabled)) {
		/* SAFE mock path: the gate is off, so we do NOT sleep. */
		pr_warn("danger_enabled=0: skipping the ILLEGAL in-softirq sleep (safe mock, no-op)\n");
		return;
	}

	/*
	 * DANGER: msleep() in softirq context sleeps while atomic. This is
	 * strictly illegal and may BUG or wedge the kernel. It exists solely
	 * to demonstrate what must NEVER be done in a tasklet/softirq.
	 */
	pr_warn("danger_enabled=1: about to sleep in softirq context -- this is ILLEGAL\n");
	msleep(100);
	pr_warn("returned from the illegal sleep; the system may now be unstable\n");
}

static irqreturn_t danger_handler(int irq, void *dev_id)
{
	tasklet_schedule(&danger_bh);
	return IRQ_HANDLED;
}

static ssize_t trigger_write(struct file *file, const char __user *ubuf,
			     size_t len, loff_t *ppos)
{
	int ret;

	ret = irq_set_irqchip_state(virq, IRQCHIP_STATE_PENDING, true);
	if (ret) {
		pr_err("failed to raise the simulated irq: %d\n", ret);
		return ret;
	}
	return len;
}

static const struct file_operations trigger_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = trigger_write,
};

static int __init sleep_in_softirq_danger_init(void)
{
	int ret;

	tasklet_setup(&danger_bh, danger_tasklet);

	sim_domain = irq_domain_create_sim(NULL, 1);
	if (IS_ERR(sim_domain)) {
		ret = PTR_ERR(sim_domain);
		pr_err("irq_domain_create_sim failed: %d\n", ret);
		goto err_kill_tasklet;
	}

	virq = irq_create_mapping(sim_domain, 0);
	if (!virq) {
		pr_err("irq_create_mapping failed\n");
		ret = -ENODEV;
		goto err_remove_sim;
	}

	ret = request_irq(virq, danger_handler, 0, KBUILD_MODNAME, NULL);
	if (ret) {
		pr_err("request_irq failed: %d\n", ret);
		goto err_dispose_mapping;
	}

	/*
	 * debugfs is this module's only interface; without it there is no way
	 * to arm or fire anything, so fail the load cleanly instead of leaving
	 * an inert module behind.
	 */
	debug_dir = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (IS_ERR(debug_dir)) {
		ret = PTR_ERR(debug_dir);
		pr_err("debugfs unavailable (%d); cannot create danger controls\n", ret);
		goto err_free_irq;
	}
	debugfs_create_bool("danger_enabled", 0600, debug_dir, &debugfs_danger_enabled);
	debugfs_create_file("trigger", 0200, debug_dir, NULL, &trigger_fops);

	pr_info("ready: set .../%s/danger_enabled to 1 then write .../trigger for the ILLEGAL path; trigger alone is a safe mock\n",
		KBUILD_MODNAME);
	return 0;

err_free_irq:
	free_irq(virq, NULL);
err_dispose_mapping:
	irq_dispose_mapping(virq);
err_remove_sim:
	irq_domain_remove_sim(sim_domain);
err_kill_tasklet:
	tasklet_kill(&danger_bh);
	return ret;
}

static void __exit sleep_in_softirq_danger_exit(void)
{
	debugfs_remove(debug_dir);
	free_irq(virq, NULL);
	irq_dispose_mapping(virq);
	irq_domain_remove_sim(sim_domain);
	tasklet_kill(&danger_bh);
	pr_info("unloaded\n");
}

module_init(sleep_in_softirq_danger_init);
module_exit(sleep_in_softirq_danger_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DANGER: deliberately sleeps in softirq context (illegal) when danger_enabled is set via debugfs");
MODULE_VERSION("1.0");
