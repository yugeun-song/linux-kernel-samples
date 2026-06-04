# SPDX-License-Identifier: 0BSD
SAMPLE_REQUIRED_CONFIGS := CONFIG_IRQ_SIM CONFIG_DEBUG_FS

# The BH (bottom-half) workqueue API (WQ_BH / system_bh_wq) landed in 6.9.
ifeq ($(notdir $(SAMPLE)),bh_workqueue)
SAMPLE_MIN_KVER := 6.9
endif
