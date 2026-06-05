# SPDX-License-Identifier: 0BSD

# timer_softirq has no top half of its own -- its bottom half is a timer_list
# callback in TIMER_SOFTIRQ -- so it needs neither irq_sim nor debugfs; it only
# needs timer_shutdown_sync(), which landed in 6.2. Every other deferred sample
# drives a simulated irq via irq_domain_create_sim(fwnode, num_irqs), the >= 5.8
# irq_sim API.
ifeq ($(notdir $(SAMPLE)),timer_softirq)
SAMPLE_MIN_KVER := 6.2
else
SAMPLE_REQUIRED_CONFIGS := CONFIG_IRQ_SIM
SAMPLE_MIN_KVER := 5.8
endif

# tasklet_setup() landed in 5.9.
ifeq ($(notdir $(SAMPLE)),tasklet)
SAMPLE_MIN_KVER := 5.9
endif

# The BH (bottom-half) workqueue API (WQ_BH / system_bh_wq) landed in 6.9.
ifeq ($(notdir $(SAMPLE)),bh_workqueue)
SAMPLE_MIN_KVER := 6.9
endif

# "workqueue" as a module name clashes with the built-in workqueue subsystem
# (/sys/module/workqueue), so the module is named workqueue_sample while the
# source file stays workqueue.c (matching the other deferred samples).
ifeq ($(notdir $(SAMPLE)),workqueue)
SAMPLE_MODULE := workqueue_sample
endif
