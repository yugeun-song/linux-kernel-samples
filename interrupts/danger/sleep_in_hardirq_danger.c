// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/*
 * WARNING: this module deliberately performs an ILLEGAL kernel operation
 * for teaching purposes: it sleeps (msleep) inside hardirq context. Hardirq
 * context is atomic and must NEVER block; doing so can hang the CPU, trip a
 * "scheduling while atomic" BUG, or wedge the machine. The illegal path runs
 * ONLY when the danger_enabled debugfs knob is set to 1; with danger_enabled=0
 * the trigger takes a SAFE mock path that does not sleep and only logs that
 * the dangerous work was skipped. Never load this on a machine you care about
 * -- use a throwaway VM.
 *
 * Verified on kernel 7.3.0-rc3 (virtme-ng config plus GPIO_SIM for irq_sim) with
 * danger_enabled=1. Captured live with ftrace function_graph
 * (set_graph_function=hardirq_danger_top_half, max_graph_depth=30) and dmesg. The
 * captured trace is longer than what is shown here; every part trimmed from the
 * real output is marked with "..." (deep callee internals -- BUG and WARNING
 * reporting, the scheduler -- and the setup/teardown calls around the context
 * switch).
 *
 * ftrace function_graph:
 *
 *  0)               |  hardirq_danger_top_half [sleep_in_hardirq_danger]() {
 *  0)               |    _printk() {
 *  0)               |      ...
 *  0) + 12.731 us   |    }
 *  0)               |    ...
 *  0)               |    msleep() {
 *  0)               |      ...
 *  0)               |      schedule_timeout_uninterruptible() {
 *  0)               |        schedule_timeout() {
 *  0)               |          ...
 *  0)               |          schedule() {
 *  0)               |            __schedule_bug() {
 *  0)               |              ...
 *  0) # 1306.749 us |            }
 *  0)               |            ...
 *  0)               |            dequeue_task_fair() {
 *  0)               |              ...
 *  0)   4.393 us    |            }
 *  0)               |            pick_task_fair() {
 *  0)               |              ...
 *  0)   0.780 us    |            }
 *  0)               |            ...
 *  0)               |            __warn() {
 *  0)               |              ...
 *  0) ! 732.991 us  |            }
 *  0)               |            ...
 *  0)               |            finish_task_switch.isra.0() {
 *  0)               |              ...
 *  0)   2.371 us    |            }
 *  0) @ 102905.5 us |          }
 *  0)               |          ...
 *  0) @ 102909.6 us |        }
 *  0) @ 102909.9 us |      }
 *  0) @ 102910.4 us |    }
 *  0)               |    ...
 *  0) @ 102943.6 us |  }
 *
 * dmesg (unreliable "?" stack-scan frames replaced with "..."):
 *
 *  sleep_in_hardirq_danger: top half: in_hardirq=Y in_softirq=N in_task=N
 *  sleep_in_hardirq_danger: danger_enabled=1: about to sleep in hardirq context -- this is illegal
 *  BUG: scheduling while atomic: sh/83/0x01000001
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
 *   hardirq_danger_top_half+0x87/0xa0 [sleep_in_hardirq_danger]
 *   ...
 *   __handle_irq_event_percpu+0x64/0x210
 *   handle_irq_event+0x41/0x90
 *   ...
 *   handle_simple_irq+0x98/0xc0
 *   irq_sim_handle_irq+0x71/0xb0
 *   irq_work_run_list+0x64/0xc0
 *   irq_work_run+0x1c/0x60
 *   __sysvec_irq_work+0x24/0xc0
 *   sysvec_irq_work+0x7c/0x90
 *   </IRQ>
 *   <TASK>
 *   asm_sysvec_irq_work+0x1f/0x30
 *   ...
 *   irq_set_irqchip_state+0xb0/0x120
 *   trigger_write+0x27/0x40 [sleep_in_hardirq_danger]
 *   ...
 *   full_proxy_write+0x68/0xb0
 *   vfs_write+0xd6/0x580
 *   ...
 *   ksys_write+0x7a/0x100
 *   ...
 *   do_syscall_64+0xcf/0x4b0
 *   entry_SYSCALL_64_after_hwframe+0x77/0x7f
 *  ...
 *  WARNING: arch/x86/kernel/process_64.c:616 at __switch_to+0x43b/0x4e0, CPU#0: sh/83
 *  ...
 *  sleep_in_hardirq_danger: returned from the illegal sleep; the system may now be unstable
 *  ...
 *  irq 59 handler hardirq_danger_top_half+0x0/0xa0 [sleep_in_hardirq_danger] enabled interrupts
 *  WARNING: kernel/irq/handle.c:214 at __handle_irq_event_percpu+0x1cc/0x210, CPU#0: sh/83
 *  ...
 *  BUG: scheduling while atomic: sh/83/0xff000001
 *  ...
 *
 * => __schedule_bug warns, but schedule() still context-switches and the CPU
 *    sleeps ~103 ms (@ 102943 us) inside hardirq, on the hardirq stack (the
 *    __switch_to WARNING, from CONFIG_DEBUG_ENTRY). The handler returns with
 *    interrupts enabled (the genirq WARNING), and the corrupted preempt_count
 *    (0xff000001) trips the BUG again in a later schedule(). The system is left
 *    unstable; with PANIC_ON_OOPS off it does not cleanly panic.
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

static irqreturn_t hardirq_danger_top_half(int irq, void *dev_id)
{
	pr_info("top half: in_hardirq=%s in_softirq=%s in_task=%s\n", in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	if (!READ_ONCE(gs_debugfs_danger_enabled)) {
		pr_warn("danger_enabled=0: skipping the illegal in-hardirq sleep (safe mock, no-op)\n");
		return IRQ_HANDLED;
	}

	/*
	 * BUG: msleep() in hardirq context sleeps while atomic. This is
	 * strictly illegal and may hang the CPU or BUG the kernel. It exists
	 * solely to demonstrate what must NEVER be done in an interrupt handler.
	 */
	pr_warn("danger_enabled=1: about to sleep in hardirq context -- this is illegal\n");
	msleep(100);
	pr_warn("returned from the illegal sleep; the system may now be unstable\n");
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

static int __init sleep_in_hardirq_danger_module_init(void)
{
	int ret;

	pr_info("init: in_hardirq=%s in_softirq=%s in_task=%s\n", in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");

	gs_sim_domain = irq_domain_create_sim(NULL, SIM_IRQ_LINES);
	if (IS_ERR(gs_sim_domain)) {
		ret = PTR_ERR(gs_sim_domain);
		pr_err("irq_domain_create_sim failed: %d\n", ret);
		return ret;
	}

	gs_virq = irq_create_mapping(gs_sim_domain, SIM_IRQ_HWIRQ);
	if (!gs_virq) {
		pr_err("irq_create_mapping failed\n");
		ret = -ENODEV;
		goto err_remove_sim;
	}

	ret = request_irq(gs_virq, hardirq_danger_top_half, 0, KBUILD_MODNAME, NULL);
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
	return ret;
}

static void __exit sleep_in_hardirq_danger_module_exit(void)
{
	pr_info("exit: in_hardirq=%s in_softirq=%s in_task=%s\n", in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	debugfs_remove(gs_debug_dir);
	free_irq(gs_virq, NULL);
	irq_dispose_mapping(gs_virq);
	irq_domain_remove_sim(gs_sim_domain);
	pr_info("unloaded\n");
}

module_init(sleep_in_hardirq_danger_module_init);
module_exit(sleep_in_hardirq_danger_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Illegal sleep in hardirq context (debugfs-gated)");
MODULE_VERSION("1.0");
