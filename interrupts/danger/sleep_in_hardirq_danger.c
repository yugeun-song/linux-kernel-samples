// SPDX-License-Identifier: 0BSD

/*
 * DANGER -- this module deliberately performs an ILLEGAL kernel operation
 * for teaching purposes: it sleeps (msleep) inside hardirq context. Hardirq
 * context is atomic and must NEVER block; doing so can hang the CPU, trip a
 * "scheduling while atomic" BUG, or wedge the machine. The illegal path runs
 * ONLY when the danger_enabled debugfs knob is set to 1; with danger_enabled=0
 * the trigger takes a SAFE mock path that does not sleep and only logs that
 * the dangerous work was skipped. Never load this on a machine you care about
 * -- use a throwaway VM.
 *
 * Verified on kernel 7.0.11 with danger_enabled=1. Captured live with ftrace
 * function_graph (set_graph_function=danger_handler, max_graph_depth=30) and
 * dmesg. The captured trace is longer than what is shown here; every part
 * trimmed from the real output is marked with "..." (deep callee internals --
 * BUG reporting, CFS scheduler -- and the setup/teardown calls around the
 * context switch).
 *
 * ftrace function_graph:
 *
 *  6)               |  danger_handler [sleep_in_hardirq_danger]() {
 *  6)               |    msleep() {
 *  6)               |      ...
 *  6)               |      schedule_timeout_uninterruptible() {
 *  6)               |        schedule_timeout() {
 *  6)               |          ...
 *  6)               |          schedule() {
 *  6)               |            __schedule_bug() {
 *  6)               |              ...
 *  6) ! 236.949 us  |            }
 *  6)               |            ...
 *  6)               |            dequeue_task_fair() {
 *  6)               |              ...
 *  6)   6.288 us    |            }
 *  6)               |            pick_next_task_fair() {
 *  6)               |              ...
 *  6)   0.597 us    |            }
 *  6)               |            ...
 *  6)               |            finish_task_switch.isra.0() {
 *  6)               |              ...
 *  6) + 10.390 us   |            }
 *  6) @ 103798.5 us |          }
 *  6)               |          ...
 *  6) @ 103803.7 us |        }
 *  6) @ 103803.9 us |      }
 *  6) @ 103804.5 us |    }
 *  6) @ 103846.3 us |  }
 *
 * dmesg (unreliable "?" stack-scan frames replaced with "..."):
 *
 *  sleep_in_hardirq_danger: danger_handler() - danger_enabled=1: about to sleep in hardirq context -- this is ILLEGAL
 *  BUG: scheduling while atomic: bash/152/0x00010002
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
 *   danger_handler+0x3d/0x60 [sleep_in_hardirq_danger 82c4602ebfecbbd081bf377a2d33f45929b35c75]
 *   ...
 *   __handle_irq_event_percpu+0x6c/0x250
 *   handle_irq_event+0x38/0x80
 *   handle_simple_irq+0xa0/0xc0
 *   irq_sim_handle_irq+0x6d/0xb0
 *   irq_work_run_list+0x6a/0xd0
 *   irq_work_run+0x18/0x50
 *   __sysvec_irq_work+0x20/0xf0
 *   ...
 *   sysvec_irq_work+0x6c/0x90
 *   </IRQ>
 *   <TASK>
 *   asm_sysvec_irq_work+0x1a/0x20
 *   __irq_put_desc_unlock+0x1c/0x50
 *   irq_set_irqchip_state+0xb2/0x120
 *   trigger_write+0x23/0x40 [sleep_in_hardirq_danger 82c4602ebfecbbd081bf377a2d33f45929b35c75]
 *   full_proxy_write+0x6f/0xc0
 *   vfs_write+0xe6/0x570
 *   ...
 *   ksys_write+0x7b/0x110
 *   do_syscall_64+0x119/0x1640
 *   ...
 *   entry_SYSCALL_64_after_hwframe+0x76/0x7e
 *
 * => __schedule_bug warns, but schedule() then really context-switches
 *    (dequeue_task_fair ... finish_task_switch) and the CPU actually sleeps
 *    ~103 ms (@ 103846 us) inside hardirq -- the system is left unstable;
 *    with PANIC_ON_OOPS off it does not cleanly panic.
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

#define SIM_IRQ_LINES 1
#define SIM_IRQ_HWIRQ 0

static struct irq_domain *sim_domain;
static struct dentry *debug_dir;
static unsigned int virq;
static bool debugfs_danger_enabled;

static irqreturn_t danger_handler(int irq, void *dev_id)
{
	if (!READ_ONCE(debugfs_danger_enabled)) {
		/*
		 * SAFE mock path: the gate is off, so we do NOT perform the
		 * illegal sleep. This is the path taken on a plain trigger.
		 */
		pr_warn("danger_enabled=0: skipping the ILLEGAL in-hardirq sleep (safe mock, no-op)\n");
		return IRQ_HANDLED;
	}

	/*
	 * DANGER: msleep() in hardirq context sleeps while atomic. This is
	 * strictly illegal and may hang the CPU or BUG the kernel. It exists
	 * solely to demonstrate what must NEVER be done in an interrupt handler.
	 */
	pr_warn("danger_enabled=1: about to sleep in hardirq context -- this is ILLEGAL\n");
	msleep(100);
	pr_warn("returned from the illegal sleep; the system may now be unstable\n");
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

static int __init sleep_in_hardirq_danger_init(void)
{
	int ret;

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
	return ret;
}

static void __exit sleep_in_hardirq_danger_exit(void)
{
	debugfs_remove(debug_dir);
	free_irq(virq, NULL);
	irq_dispose_mapping(virq);
	irq_domain_remove_sim(sim_domain);
	pr_info("unloaded\n");
}

module_init(sleep_in_hardirq_danger_init);
module_exit(sleep_in_hardirq_danger_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DANGER: deliberately sleeps in hardirq context (illegal) when danger_enabled is set via debugfs");
MODULE_VERSION("1.0");
