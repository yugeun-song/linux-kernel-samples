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

SAMPLE_DIRS := \
	smp/percpu \
	interrupts/hardirq \
	interrupts/deferred \
	interrupts/danger \
	interrupts/concurrency \
	data_structure

SAMPLES :=
define import_manifest
ifeq ($$(wildcard $(1)/manifest.mk),)
$$(error $(1)/manifest.mk: not found (listed in SAMPLE_DIRS))
endif
samples :=
include $(1)/manifest.mk
ifeq ($$(strip $$(samples)),)
$$(error $(1)/manifest.mk: samples is empty)
endif
SAMPLES += $$(addprefix $(1)/,$$(samples))
endef
$(foreach d,$(SAMPLE_DIRS),$(eval $(call import_manifest,$(d))))

PASS := KVER='$(KVER)' KDIR='$(KDIR)'
ifneq ($(ARCH),)
PASS += ARCH='$(ARCH)'
endif
ifneq ($(CROSS_COMPILE),)
PASS += CROSS_COMPILE='$(CROSS_COMPILE)'
endif

SUBMAKE = $(MAKE) -f $(DRIVER) $(PASS)

.PHONY: all clean list tags cscope compdb target $(SAMPLES)

all: $(SAMPLES) compdb tags cscope

compdb: $(SAMPLES)
	python3 scripts/compdb.py

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
