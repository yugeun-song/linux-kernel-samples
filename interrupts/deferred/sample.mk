# SPDX-License-Identifier: 0BSD

# net_rx_softirq observes packets from a netfilter hook, and timer_softirq's
# bottom half is a timer_list callback; every other deferred sample drives a
# simulated IRQ.
ifeq ($(notdir $(SAMPLE)),net_rx_softirq)
SAMPLE_REQUIRED_CONFIGS := CONFIG_NETFILTER
else ifneq ($(notdir $(SAMPLE)),timer_softirq)
SAMPLE_REQUIRED_CONFIGS := CONFIG_IRQ_SIM
endif
