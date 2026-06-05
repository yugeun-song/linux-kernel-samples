# SPDX-License-Identifier: 0BSD
SAMPLE_REQUIRED_CONFIGS := CONFIG_IRQ_SIM CONFIG_DEBUG_FS

# Both danger modules drive a simulated irq (>= 5.8 irq_sim API). The softirq one
# also uses tasklet_setup(), which landed in 5.9.
ifeq ($(notdir $(SAMPLE)),sleep_in_softirq_danger)
SAMPLE_MIN_KVER := 5.9
else
SAMPLE_MIN_KVER := 5.8
endif
