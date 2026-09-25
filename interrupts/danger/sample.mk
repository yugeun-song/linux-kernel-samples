# SPDX-License-Identifier: 0BSD
SAMPLE_REQUIRED_CONFIGS := CONFIG_IRQ_SIM CONFIG_DEBUG_FS

# in_hardirq() landed in 5.11, after the 5.8 irq_sim API and 5.9 tasklet_setup().
SAMPLE_MIN_KVER := 5.11
