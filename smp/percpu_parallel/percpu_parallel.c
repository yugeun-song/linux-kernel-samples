#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>
#include <linux/err.h>

#define INTERVAL_MS 1000

static DEFINE_PER_CPU(long, counter);
static DEFINE_PER_CPU(struct task_struct *, thread);

static int thread_routine(void *data)
{
	unsigned int cpu = smp_processor_id();

	while (!kthread_should_stop()) {
		long val;

		val = this_cpu_read(counter);
		this_cpu_inc(counter);

		pr_info("percpu_parallel: cpu%u=%ld\n", cpu, val);

		set_current_state(TASK_INTERRUPTIBLE);
		if (kthread_should_stop()) {
			set_current_state(TASK_RUNNING);
			break;
		}
		schedule_timeout(msecs_to_jiffies(INTERVAL_MS));
	}

	return 0;
}

static void stop_all_threads(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		struct task_struct *task = per_cpu(thread, cpu);

		if (task) {
			kthread_stop(task);
			per_cpu(thread, cpu) = NULL;
		}
	}
}

static int __init percpu_parallel_init(void)
{
	unsigned int cpu;

	pr_info("percpu_parallel: started interval=%ums\n", INTERVAL_MS);

	for_each_online_cpu(cpu) {
		per_cpu(counter, cpu) = (long)(cpu + 1) * 100;
	}

	for_each_online_cpu(cpu) {
		struct task_struct *task;

		task = kthread_create(thread_routine, NULL, "percpu_parallel/%u", cpu);
		if (IS_ERR(task)) {
			pr_err("percpu_parallel: failed on cpu%u: %ld\n",
			       cpu, PTR_ERR(task));
			stop_all_threads();
			return PTR_ERR(task);
		}

		kthread_bind(task, cpu);
		per_cpu(thread, cpu) = task;
		wake_up_process(task);
	}

	return 0;
}

static void __exit percpu_parallel_exit(void)
{
	stop_all_threads();
	pr_info("percpu_parallel: stopped\n");
}

module_init(percpu_parallel_init);
module_exit(percpu_parallel_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yugeun Song");
MODULE_DESCRIPTION("Per-CPU bound kthreads, each updating only its own per-CPU counter lock-free");
MODULE_VERSION("1.0");
