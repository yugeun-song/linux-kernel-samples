# SPDX-License-Identifier: 0BSD
HOST_KVER     := $(shell uname -r)
HOST_ARCH     := $(shell uname -m)
KVER          ?= $(HOST_KVER)
KDIR          ?= /lib/modules/$(KVER)/build
ARCH          ?=
CROSS_COMPILE ?=

-include config.mk

ifeq ($(origin KVER),command line)
TARGET_SRC := command line
else ifneq ($(KVER),$(HOST_KVER))
TARGET_SRC := config.mk
else
TARGET_SRC := host default
endif

ifneq ($(ARCH),)
TARGET_ARCH := $(ARCH)
else
TARGET_ARCH := $(HOST_ARCH) (native)
endif

DRIVER  := scripts/kmod.mk

SAMPLES := \
	smp/percpu/percpu_parallel \
	interrupts/hardirq/hardirq \
	interrupts/hardirq/irq_none \
	interrupts/hardirq/disable_irq \
	interrupts/deferred/tasklet \
	interrupts/deferred/bh_workqueue \
	interrupts/deferred/workqueue \
	interrupts/deferred/threaded_irq \
	interrupts/deferred/timer_softirq \
	interrupts/danger/sleep_in_hardirq_danger \
	interrupts/danger/sleep_in_softirq_danger

PASS := KVER='$(KVER)' KDIR='$(KDIR)'
ifneq ($(ARCH),)
PASS += ARCH='$(ARCH)'
endif
ifneq ($(CROSS_COMPILE),)
PASS += CROSS_COMPILE='$(CROSS_COMPILE)'
endif

SUBMAKE = $(MAKE) -f $(DRIVER) $(PASS)

.PHONY: all clean list tags cscope target $(SAMPLES)
.NOTPARALLEL:

all: $(SAMPLES)

target:
	@echo '=> target: KVER=$(KVER) ARCH=$(TARGET_ARCH) ($(TARGET_SRC))'

$(SAMPLES): | target
	$(SUBMAKE) build SAMPLE=$@

clean:
	@for s in $(SAMPLES); do $(SUBMAKE) clean SAMPLE=$$s; done

list:
	@for s in $(SAMPLES); do echo "$$s"; done

tags:
	ctags -R --exclude=.git .

cscope:
	cscope -Rbq
