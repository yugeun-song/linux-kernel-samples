# SPDX-License-Identifier: 0BSD
# in_hardirq() landed in 5.11, after the 5.8 irq_sim API.
SAMPLE_REQUIRED_CONFIGS := CONFIG_IRQ_SIM
SAMPLE_MIN_KVER := 5.11
