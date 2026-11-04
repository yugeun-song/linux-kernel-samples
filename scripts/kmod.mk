# SPDX-License-Identifier: 0BSD
SAMPLE        ?=
KVER          ?= $(shell uname -r)
KDIR          ?= /lib/modules/$(KVER)/build
ARCH          ?=
CROSS_COMPILE ?=
LLVM          ?=
PROFILE       ?= debug
SIGN          ?= 1
SIGN_KEY      ?= $(CURDIR)/MOK.priv
SIGN_CERT     ?= $(CURDIR)/MOK.der
SIGN_SCRIPT   := $(dir $(firstword $(MAKEFILE_LIST)))sign-module.sh
KTREE_SCRIPT  := $(dir $(firstword $(MAKEFILE_LIST)))ktree.py
PROFILE_MK    := $(abspath $(dir $(firstword $(MAKEFILE_LIST)))profile.mk)

ifeq ($(SAMPLE),)
$(error SAMPLE is not set)
endif

src := $(abspath $(dir $(SAMPLE)))
src_base := $(notdir $(SAMPLE))

SAMPLE_MIN_KVER := $(KVER_BASELINE)
-include $(src)/sample.mk

ifdef SAMPLE_MODULE
mod := $(SAMPLE_MODULE)
else
mod := $(src_base)
endif

bdir := $(src)/.build-$(mod)
srcs := $(if $(SAMPLE_OBJS),$(SAMPLE_OBJS:.o=.c),$(src_base).c)
mod_objs := $(or $(SAMPLE_OBJS),$(if $(filter-out $(mod),$(src_base)),$(src_base).o))
KBUILD_ARGS := M=$(bdir)
ifneq ($(ARCH),)
KBUILD_ARGS += ARCH=$(ARCH)
endif
ifneq ($(CROSS_COMPILE),)
KBUILD_ARGS += CROSS_COMPILE=$(CROSS_COMPILE)
endif
ifneq ($(LLVM),)
KBUILD_ARGS += LLVM=$(LLVM)
endif
ifeq ($(PROFILE),debug)
KBUILD_ARGS += W=1
endif
ktree := $(realpath $(KDIR))
objcopy := $(if $(LLVM),$(filter %/,$(LLVM))llvm-objcopy$(filter -%,$(LLVM)),$(CROSS_COMPILE)objcopy)

ifneq ($(filter build,$(MAKECMDGOALS)),)

ifndef SAMPLE_OBJS
ifeq ($(wildcard $(src)/$(src_base).c),)
$(error $(SAMPLE): expected module source $(src_base).c (set SAMPLE_OBJS in sample.mk))
endif
endif

ifeq ($(wildcard $(KDIR)/Makefile),)
$(error kernel build tree not found: KDIR=$(KDIR) (set KVER= or KDIR=))
endif

ifdef SAMPLE_SUPPORTED_ARCH
ifeq ($(filter $(SAMPLE_SUPPORTED_ARCH),$(KSRCARCH)),)
$(error $(SAMPLE) supports ARCH in [$(SAMPLE_SUPPORTED_ARCH)] (kbuild ARCH names); \
	the target is $(or $(KSRCARCH),unknown))
endif
endif

ifdef SAMPLE_REQUIRED_CONFIGS
missing := $(strip $(foreach c,$(SAMPLE_REQUIRED_CONFIGS),\
	$(if $(shell grep -hs '^$(c)=[ym]' $(KDIR)/.config),,$(c))))
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
$(error $(SAMPLE) requires kernel <= $(SAMPLE_MAX_KVER) (feature removed in a later kernel); \
	target kernel is $(target_rel))
endif
endif

endif

.PHONY: build

build:
	@[ "$$(cat $(bdir)/.kdir 2>/dev/null)" = '$(ktree)' ] || \
		rm -rf $(bdir) $(src)/$(mod).ko $(src)/$(mod).ko.debug
	@mkdir -p $(bdir)
	@echo '$(ktree)' > $(bdir)/.kdir
	@$(if $(KBUILD_EXTRA_SYMBOLS),rm -f $(bdir)/Module.symvers,:)
	@for c in $(srcs); do ln -sf $(src)/$$c $(bdir)/$$c; done
	@printf '%s\n' 'PROFILE := $(PROFILE)' 'include $(PROFILE_MK)' 'obj-m := $(mod).o' > $(bdir)/Kbuild
	@$(if $(mod_objs),echo '$(mod)-objs := $(mod_objs)' >> $(bdir)/Kbuild,:)
	$(MAKE) -C $(KDIR) $(KBUILD_ARGS) modules
	@python3 '$(KTREE_SCRIPT)' elf '$(KDIR)' '$(bdir)/$(mod).ko'
	@cp $(bdir)/$(mod).ko $(src)/$(mod).ko
ifeq ($(PROFILE),release)
	@$(objcopy) --only-keep-debug $(src)/$(mod).ko $(src)/$(mod).ko.debug
	@$(objcopy) --strip-debug --add-gnu-debuglink=$(src)/$(mod).ko.debug $(src)/$(mod).ko
else
	@rm -f $(src)/$(mod).ko.debug
endif
ifeq ($(SIGN),1)
	@sh '$(SIGN_SCRIPT)' '$(KDIR)/scripts/sign-file' '$(SIGN_KEY)' '$(SIGN_CERT)' \
		'$(src)/$(mod).ko'
endif
