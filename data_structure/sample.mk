# SPDX-License-Identifier: 0BSD

# Baseline is the 6.12 LTS kernel.
ifeq ($(notdir $(SAMPLE)),list)
SAMPLE_MIN_KVER := 6.12
endif
