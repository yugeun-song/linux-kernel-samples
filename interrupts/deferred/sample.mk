# SPDX-License-Identifier: 0BSD
SAMPLE_REQUIRED_CONFIGS := CONFIG_IRQ_SIM CONFIG_DEBUG_FS

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
