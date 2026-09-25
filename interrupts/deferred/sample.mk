# SPDX-License-Identifier: 0BSD

# Every deferred sample logs in_hardirq(), which landed in 5.11.
SAMPLE_MIN_KVER := 5.11

# timer_softirq and tcp_softirq_log are the deferred samples that do NOT drive a
# simulated irq: timer_softirq's bottom half is a timer_list callback in
# TIMER_SOFTIRQ (needs timer_shutdown_sync(), 6.2), and tcp_softirq_log observes
# packets from a netfilter hook (CONFIG_NETFILTER). Every other deferred sample
# drives a simulated irq via irq_domain_create_sim(fwnode, num_irqs).
ifeq ($(notdir $(SAMPLE)),timer_softirq)
SAMPLE_MIN_KVER := 6.2
else ifeq ($(notdir $(SAMPLE)),tcp_softirq_log)
SAMPLE_REQUIRED_CONFIGS := CONFIG_NETFILTER
else
SAMPLE_REQUIRED_CONFIGS := CONFIG_IRQ_SIM
endif

# The BH (bottom-half) workqueue API (WQ_BH / system_bh_wq) landed in 6.9.
ifeq ($(notdir $(SAMPLE)),bh_workqueue)
SAMPLE_MIN_KVER := 6.9
endif
