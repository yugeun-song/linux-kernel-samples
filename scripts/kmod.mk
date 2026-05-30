SAMPLE        ?=
KVER          ?= $(shell uname -r)
KDIR          ?= /lib/modules/$(KVER)/build
ARCH          ?=
CROSS_COMPILE ?=

ifeq ($(SAMPLE),)
$(error SAMPLE is not set)
endif

src := $(abspath $(SAMPLE))

-include $(src)/sample.mk

ifdef SAMPLE_MODULE
mod := $(SAMPLE_MODULE)
else
mod := $(notdir $(SAMPLE))
endif

KBUILD_ARGS := M=$(src)
ifneq ($(ARCH),)
KBUILD_ARGS += ARCH=$(ARCH)
endif
ifneq ($(CROSS_COMPILE),)
KBUILD_ARGS += CROSS_COMPILE=$(CROSS_COMPILE)
endif

ifneq ($(filter build,$(MAKECMDGOALS)),)

ifndef SAMPLE_OBJS
ifeq ($(wildcard $(src)/$(mod).c),)
$(error $(SAMPLE): expected module source $(mod).c (set SAMPLE_MODULE/SAMPLE_OBJS in sample.mk))
endif
endif

ifeq ($(wildcard $(KDIR)/Makefile),)
$(error kernel build tree not found: KDIR=$(KDIR) (set KVER= or KDIR=))
endif

ifneq ($(CROSS_COMPILE),)
ifeq ($(shell command -v $(CROSS_COMPILE)gcc 2>/dev/null),)
$(error cross compiler not found: $(CROSS_COMPILE)gcc not in PATH)
endif
endif

ifdef SAMPLE_SUPPORTED_ARCH
eff_arch := $(if $(ARCH),$(ARCH),$(shell uname -m))
ifeq ($(filter $(SAMPLE_SUPPORTED_ARCH),$(eff_arch)),)
$(error $(SAMPLE) supports ARCH in [$(SAMPLE_SUPPORTED_ARCH)]; effective arch is $(eff_arch))
endif
endif

ifdef SAMPLE_REQUIRED_CONFIGS
missing := $(strip $(foreach c,$(SAMPLE_REQUIRED_CONFIGS),$(if $(shell grep -hs '^$(c)=[ym]' $(KDIR)/.config),,$(c))))
ifneq ($(missing),)
$(error $(SAMPLE): required kernel configs not enabled in $(KDIR): $(missing))
endif
endif

ifdef SAMPLE_MIN_KVER
target_rel := $(shell cat $(KDIR)/include/config/kernel.release 2>/dev/null)
lowest := $(shell printf '%s\n%s\n' '$(SAMPLE_MIN_KVER)' '$(target_rel)' | sort -V | head -n1)
ifneq ($(lowest),$(SAMPLE_MIN_KVER))
$(error $(SAMPLE) requires kernel >= $(SAMPLE_MIN_KVER); target kernel is $(target_rel))
endif
endif

endif

.PHONY: build clean

build:
	@echo 'obj-m := $(mod).o' > $(src)/Kbuild
	@$(if $(SAMPLE_OBJS),echo '$(mod)-objs := $(SAMPLE_OBJS)' >> $(src)/Kbuild)
	$(MAKE) -C $(KDIR) $(KBUILD_ARGS) modules

clean:
	@echo 'obj-m := $(mod).o' > $(src)/Kbuild
	-$(MAKE) -C $(KDIR) $(KBUILD_ARGS) clean
	@rm -f $(src)/Kbuild
