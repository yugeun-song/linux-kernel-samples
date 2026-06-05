# SPDX-License-Identifier: 0BSD
SAMPLE        ?=
KVER          ?= $(shell uname -r)
KDIR          ?= /lib/modules/$(KVER)/build
ARCH          ?=
CROSS_COMPILE ?=
SIGN          ?= 1
SIGN_KEY      ?= $(CURDIR)/MOK.priv
SIGN_CERT     ?= $(CURDIR)/MOK.der
SIGN_SCRIPT   := $(dir $(firstword $(MAKEFILE_LIST)))sign-module.sh

ifeq ($(SAMPLE),)
$(error SAMPLE is not set)
endif

src := $(abspath $(dir $(SAMPLE)))
src_base := $(notdir $(SAMPLE))

-include $(src)/sample.mk

ifdef SAMPLE_MODULE
mod := $(SAMPLE_MODULE)
else
mod := $(src_base)
endif

bdir := $(src)/.build-$(mod)
srcs := $(if $(SAMPLE_OBJS),$(SAMPLE_OBJS:.o=.c),$(src_base).c)
KBUILD_ARGS := M=$(bdir)
ifneq ($(ARCH),)
KBUILD_ARGS += ARCH=$(ARCH)
endif
ifneq ($(CROSS_COMPILE),)
KBUILD_ARGS += CROSS_COMPILE=$(CROSS_COMPILE)
endif

ifneq ($(filter build,$(MAKECMDGOALS)),)

ifndef SAMPLE_OBJS
ifeq ($(wildcard $(src)/$(src_base).c),)
$(error $(SAMPLE): expected module source $(src_base).c (set SAMPLE_OBJS in sample.mk))
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
ifneq ($(ARCH),)
eff_arch := $(ARCH)
else
eff_arch := $(shell uname -m)
eff_arch := $(patsubst i%86,x86,$(eff_arch))
eff_arch := $(patsubst x86_64,x86,$(eff_arch))
eff_arch := $(patsubst aarch64,arm64,$(eff_arch))
eff_arch := $(patsubst armv%,arm,$(eff_arch))
eff_arch := $(patsubst riscv%,riscv,$(eff_arch))
eff_arch := $(patsubst ppc64le,powerpc,$(eff_arch))
eff_arch := $(patsubst ppc%,powerpc,$(eff_arch))
eff_arch := $(patsubst s390x,s390,$(eff_arch))
endif
ifeq ($(filter $(SAMPLE_SUPPORTED_ARCH),$(eff_arch)),)
$(error $(SAMPLE) supports ARCH in [$(SAMPLE_SUPPORTED_ARCH)] (kbuild ARCH names); effective arch is $(eff_arch))
endif
endif

ifdef SAMPLE_REQUIRED_CONFIGS
missing := $(strip $(foreach c,$(SAMPLE_REQUIRED_CONFIGS),$(if $(shell grep -hs '^$(c)=[ym]' $(KDIR)/.config),,$(c))))
ifneq ($(missing),)
$(error $(SAMPLE): required kernel configs not enabled in $(KDIR): $(missing))
endif
endif

ifneq ($(SAMPLE_MIN_KVER)$(SAMPLE_MAX_KVER),)
target_rel := $(shell cat $(KDIR)/include/config/kernel.release 2>/dev/null)
target_mm := $(shell printf '%s' '$(target_rel)' | grep -oE '^[0-9]+\.[0-9]+')
endif

ifdef SAMPLE_MIN_KVER
lowest := $(shell printf '%s\n%s\n' '$(SAMPLE_MIN_KVER)' '$(target_mm)' | sort -V | head -n1)
ifneq ($(lowest),$(SAMPLE_MIN_KVER))
$(error $(SAMPLE) requires kernel >= $(SAMPLE_MIN_KVER); target kernel is $(target_rel))
endif
endif

ifdef SAMPLE_MAX_KVER
highest := $(shell printf '%s\n%s\n' '$(SAMPLE_MAX_KVER)' '$(target_mm)' | sort -V | tail -n1)
ifneq ($(highest),$(SAMPLE_MAX_KVER))
$(error $(SAMPLE) requires kernel <= $(SAMPLE_MAX_KVER) (feature removed in a later kernel); target kernel is $(target_rel))
endif
endif

endif

.PHONY: build clean

build:
	@mkdir -p $(bdir)
	@for c in $(srcs); do ln -sf $(src)/$$c $(bdir)/$$c; done
	@echo 'obj-m := $(mod).o' > $(bdir)/Kbuild
	@$(if $(SAMPLE_OBJS),echo '$(mod)-objs := $(SAMPLE_OBJS)' >> $(bdir)/Kbuild,$(if $(filter-out $(mod),$(src_base)),echo '$(mod)-objs := $(src_base).o' >> $(bdir)/Kbuild,:))
	$(MAKE) -C $(KDIR) $(KBUILD_ARGS) modules
	@cp $(bdir)/$(mod).ko $(src)/$(mod).ko
ifeq ($(SIGN),1)
	@sh '$(SIGN_SCRIPT)' '$(KDIR)/scripts/sign-file' '$(SIGN_KEY)' '$(SIGN_CERT)' '$(src)/$(mod).ko'
endif

clean:
	@rm -rf $(bdir)
	@rm -f $(src)/$(mod).ko
