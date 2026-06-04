# SPDX-License-Identifier: 0BSD
KVER          ?= $(shell uname -r)
KDIR          ?= /lib/modules/$(KVER)/build
ARCH          ?=
CROSS_COMPILE ?=

-include config.mk

DRIVER  := scripts/kmod.mk

SAMPLES := \
	smp/percpu/percpu_parallel \
	interrupts/hardirq/hardirq \
	interrupts/deferred/tasklet \
	interrupts/deferred/bh_workqueue \
	interrupts/deferred/workqueue_demo \
	interrupts/deferred/threaded_irq \
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

.PHONY: all clean list tags cscope $(SAMPLES)
.NOTPARALLEL:

all: $(SAMPLES)

$(SAMPLES):
	$(SUBMAKE) build SAMPLE=$@

clean:
	@for s in $(SAMPLES); do $(SUBMAKE) clean SAMPLE=$$s; done

list:
	@for s in $(SAMPLES); do echo "$$s"; done

tags:
	ctags -R --exclude=.git .

cscope:
	cscope -Rbq
