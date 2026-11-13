# SPDX-License-Identifier: 0BSD
HOST_KVER     := $(shell uname -r)
KVER          ?= $(HOST_KVER)
KDIR          ?= /lib/modules/$(KVER)/build
ARCH          ?=
CROSS_COMPILE ?=
LLVM          ?=
VMLINUX       ?=
PROFILE       ?= debug
NPROC         ?= $(shell nproc)
KALIAS_DIRS   ?= /usr/src
BASELINE      := 6.12

-include config.mk

ifneq ($(filter-out 0 1,$(VMLINUX)),)
$(error VMLINUX takes 0 or 1, not '$(VMLINUX)')
endif
ifneq ($(words $(filter debug release,$(PROFILE))) $(words $(PROFILE)),1 1)
$(error PROFILE takes debug or release, not '$(PROFILE)')
endif

ifeq ($(filter -j%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(NPROC)
endif

ifneq ($(word 2,$(MAKECMDGOALS)),)

.PHONY: $(MAKECMDGOALS) run-goals-in-order

$(MAKECMDGOALS): run-goals-in-order
	@:

run-goals-in-order:
	@set -e; for goal in $(MAKECMDGOALS); do $(MAKE) --no-print-directory $$goal; done

else

comma := ,
kinfo = $(patsubst $(1)=%,%,$(filter $(1)=%,$(KINFO)))

ifeq ($(KDIR),/lib/modules/$(KVER)/build)
ifeq ($(origin KVER),command line)
TARGET_SRC := KVER from the command line
else ifneq ($(KVER),$(HOST_KVER))
TARGET_SRC := KVER from config.mk
else ifeq ($(wildcard $(KDIR)/Makefile),)
NEWEST_KVER := $(notdir $(patsubst %/build,%,$(lastword $(shell ls -d /lib/modules/*/build 2>/dev/null | sort -V))))
KVER := $(or $(NEWEST_KVER),$(HOST_KVER))
TARGET_SRC := newest installed; running $(HOST_KVER) has no build tree
else
TARGET_SRC := running kernel
endif
else
TARGET_SRC := KDIR
endif
KTREE := $(abspath $(or $(wildcard $(KDIR)),$(KDIR)))

ifneq ($(filter-out clean clean-index distclean list help sanitize debug release,$(or $(MAKECMDGOALS),all)),)
KINFO := $(shell python3 scripts/ktree.py check --baseline $(BASELINE) --arch '$(ARCH)' \
	--cross '$(CROSS_COMPILE)' --llvm '$(LLVM)' \
	--cc '$(if $(filter-out default,$(origin CC)),$(CC))' '$(KTREE)')
ifneq ($(.SHELLSTATUS),0)
$(error $(KINFO))
endif
endif

ARCH := $(or $(ARCH),$(call kinfo,arch))
ifeq ($(CROSS_COMPILE)$(LLVM)$(filter-out default,$(origin CC)),)
CROSS_COMPILE := $(call kinfo,cross)
LLVM          := $(call kinfo,llvm)
KCC           := $(call kinfo,cc)
endif

KBUILD_DIR := $(KTREE)
ifeq ($(call kinfo,symvers),vmlinux)
MODPOST    := KBUILD_EXTRA_SYMBOLS='$(KTREE)/vmlinux.symvers'
TARGET_SRC := $(TARGET_SRC); exports from vmlinux.symvers
else ifeq ($(call kinfo,symvers),none)
KOBJ := $(CURDIR)/.kbuild/$(call kinfo,id)
ifneq ($(call kinfo,tools),)
$(error $(KTREE) was configured with another toolchain ($(subst $(comma), ,$(call kinfo,tools))), \
	so building vmlinux here would rewrite its configuration)
endif
ifeq ($(wildcard $(KOBJ)/obj/vmlinux.symvers),)
ifneq ($(VMLINUX),1)
$(error $(KTREE) has no Module.symvers or vmlinux.symvers, so the modules cannot be checked \
	against it; VMLINUX=1 builds vmlinux out of tree in .kbuild/ from its sources and .config)
endif
ifeq ($(call kinfo,sources),)
$(error $(KTREE) has no kernel sources, so vmlinux cannot be built for it)
endif
KPREP := $(KOBJ)/obj/vmlinux.symvers
else ifneq ($(call kinfo,release),)
ifneq ($(call kinfo,release),$(shell cat '$(KOBJ)/obj/include/config/kernel.release'))
$(error $(KOBJ:$(CURDIR)/%=%) holds a different release than $(KTREE); remove it to rebuild)
endif
endif
KSRC       := $(if $(call kinfo,clean),$(call kinfo,src),$(KOBJ)/src)
KBUILD_DIR := $(KOBJ)/obj
MODPOST    := KBUILD_EXTRA_SYMBOLS='$(KOBJ)/obj/vmlinux.symvers'
TARGET_SRC := $(TARGET_SRC); vmlinux built in $(KOBJ:$(CURDIR)/%=%)
endif
ifneq ($(call kinfo,fallback),)
TARGET_SRC := $(TARGET_SRC)$(foreach f,$(subst $(comma), ,$(call kinfo,fallback)),; $(firstword \
	$(subst :, ,$(f))) is missing$(comma) using $(lastword $(subst :, ,$(f))))
endif
ifneq ($(call kinfo,tools),)
TARGET_SRC := $(TARGET_SRC); toolchain differs from its config: $(subst $(comma), ,$(call kinfo,tools))
endif
ifeq ($(call kinfo,elf)$(KOBJ),none)
TARGET_SRC := $(TARGET_SRC); no vmlinux or empty.o there to check the modules against
endif
SIGN ?= $(if $(filter $(HOST_KVER),$(call kinfo,release)),1,0)

TOOLCHAIN := $(if $(LLVM), LLVM=$(LLVM))$(if $(CROSS_COMPILE), CROSS_COMPILE=$(CROSS_COMPILE))$(if \
	$(KCC), CC=$(KCC))
KBUILD_TC := $(if $(ARCH),ARCH='$(ARCH)') $(if $(CROSS_COMPILE),CROSS_COMPILE='$(CROSS_COMPILE)') \
	$(if $(LLVM),LLVM='$(LLVM)') $(if $(KCC),CC='$(KCC)')

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

PASS := KDIR='$(KBUILD_DIR)' $(KBUILD_TC) SIGN='$(SIGN)' PROFILE='$(PROFILE)' \
	KVER_BASELINE='$(BASELINE)' KSRCARCH='$(call kinfo,srcarch)' $(MODPOST)

SUBMAKE = $(MAKE) -f $(DRIVER) $(PASS)
COMPDB  = python3 scripts/compdb.py --kdir '$(KBUILD_DIR)' --profile '$(PROFILE)' --link-dirs '$(KALIAS_DIRS)' \
	$(if $(filter $(KOBJ)/src,$(KSRC)),--map '$(KSRC)=$(call kinfo,src)')

.PHONY: all debug release sanitize index clean-index clean distclean help list tags cscope cscope.files \
	compdb target $(SAMPLES)

all: $(SAMPLES)
	@echo "=> $(PROFILE): $(words $(SAMPLES)) samples built against" \
		"$$(cat '$(KBUILD_DIR)/include/config/kernel.release')"

debug:
	@$(MAKE) --no-print-directory PROFILE=debug all

release:
	@$(MAKE) --no-print-directory PROFILE=release all

sanitize:
	@echo "=> sanitize: kernel modules take their sanitizers from the target kernel's configuration" \
		"(CONFIG_KASAN, CONFIG_UBSAN, CONFIG_KCSAN); build against such a tree with KDIR="

index: compdb tags cscope

compdb: $(SAMPLES)
	$(COMPDB)

target: | $(KPREP)
	@printf '=> target: KVER=%s ARCH=%s PROFILE=%s (%s)\n' \
		"$$(cat '$(KBUILD_DIR)/include/config/kernel.release')" '$(ARCH)$(TOOLCHAIN)' \
		'$(PROFILE)' '$(TARGET_SRC)'

$(SAMPLES): | target $(KPREP)
	$(SUBMAKE) build SAMPLE=$@

ifneq ($(KPREP),)
$(KPREP):
	@echo "=> vmlinux: building for $(KTREE) in $(KOBJ) from its .config, the tree is only read"
	rm -rf '$(KOBJ)'
	mkdir -p '$(KOBJ)/obj'
ifeq ($(KSRC),$(KOBJ)/src)
	cp -a --reflink=auto '$(call kinfo,src)' '$(KSRC)'
	$(MAKE) -C '$(KSRC)' mrproper
endif
	cp '$(KTREE)/.config' '$(KOBJ)/obj/.config'
	$(MAKE) -C '$(KSRC)' O='$(KOBJ)/obj' $(KBUILD_TC) syncconfig
	@cmp -s '$(KTREE)/.config' '$(KOBJ)/obj/.config' || { diff '$(KTREE)/.config' \
		'$(KOBJ)/obj/.config' | grep '^[<>]' | head -n8; rm -rf '$(KOBJ)'; echo "the" \
		"configuration would change with this toolchain; refusing to build"; exit 1; }
	$(MAKE) -C '$(KSRC)' O='$(KOBJ)/obj' $(KBUILD_TC) vmlinux modules_prepare
	@python3 scripts/ktree.py check --baseline $(BASELINE) --arch '$(ARCH)' \
		--release '$(call kinfo,release)' --elf '$(call kinfo,elf)' '$(KOBJ)/obj' > /dev/null \
		|| { python3 scripts/ktree.py check --baseline $(BASELINE) --arch '$(ARCH)' \
		--release '$(call kinfo,release)' --elf '$(call kinfo,elf)' '$(KOBJ)/obj'; \
		rm -rf '$(KOBJ)'; exit 1; }
endif

clean:
	@find . -path ./.git -prune -o -path ./.kbuild -prune -o -name '.build-*' -type d -prune \
		-exec rm -rf {} + -o \( -name '*.ko' -o -name '*.ko.debug' \) -type f -exec rm -f {} +
	@rm -rf .kbuild
	@echo "=> clean: removed the modules, .ko.debug files, .build-* directories and .kbuild/"

clean-index:
	@rm -f compile_commands.json compile_commands.json.tmp cscope.files cscope.files.tmp tags \
		cscope.out cscope.in.out cscope.po.out
	@echo "=> clean-index: removed compile_commands.json, tags and cscope.*"

distclean: clean clean-index
	@rm -rf .cache .clangd gmon.out perf.data perf.data.old uftrace.data uftrace.data.old core \
		core.[0-9]* vgcore.[0-9]*
	@rm -f config.mk MOK.der MOK.priv
	@echo "=> distclean: removed .cache/, .clangd/, tool outputs, config.mk and the signing key (MOK.der, MOK.priv)"

help:
	@printf '%s\n' \
		'usage: make [goal ...] [VARIABLE=value ...]' \
		'' \
		'goals:' \
		'  all           every registered sample with the PROFILE flags' \
		'  debug         all with PROFILE=debug: -DDEBUG -g3, -fstack-protector-all, pattern init, GCOV, KCOV' \
		'  release       all with PROFILE=release: the kernel flags plus -g, debug info split into .ko.debug' \
		'  sanitize      explain where kernel sanitizers come from' \
		'  index         compile_commands.json with the PROFILE flags, tags and cscope.out' \
		'  compdb        compile_commands.json and cscope.files only (tags and cscope make the others)' \
		'  clean-index   remove compile_commands.json, tags and cscope.*' \
		'  clean         remove the modules, .ko.debug files, .build-* directories and .kbuild/' \
		'  distclean     clean, clean-index, clangd caches, tool outputs, config.mk and the signing key' \
		'  list          list the registered samples' \
		'  <dir>/<name>  one sample, for example $(firstword $(SAMPLES))' \
		'  help          print this help' \
		'' \
		'variables:' \
		'  PROFILE=debug|release         what all builds and which flags index uses (default debug)' \
		'  KVER=<release>                kernel release under /lib/modules (default: the running one)' \
		'  KDIR=<dir>                    kernel build tree instead of /lib/modules/KVER/build' \
		'  ARCH=, CROSS_COMPILE=, LLVM=  target and toolchain, as in Kbuild (default: from the tree)' \
		'  VMLINUX=0|1                   build vmlinux in .kbuild/ for a configured or prepared tree' \
		'  SIGN=0|1                      sign the modules with MOK.priv (default: 1 for the running kernel)' \
		'' \
		'Goals run in the order given. config.mk can hold any of these variables.'

list:
	@for s in $(SAMPLES); do echo "$$s"; done

cscope.files: compdb

tags: cscope.files
	@if command -v ctags > /dev/null 2>&1; then \
	    grep '^/' cscope.files | ctags -R --kinds-C=+p --kinds-C++=+p --exclude=.git \
	        --exclude=.cache --exclude=.kbuild --exclude='.build-*' --exclude=compile_commands.json \
	        -L - . && echo "=> tags: the repository and $$(grep -c '^/' cscope.files) kernel headers"; \
	else \
	    echo "=> tags: skipped, ctags not found"; \
	fi

cscope: cscope.files
	@if command -v cscope > /dev/null 2>&1; then \
	    cscope -bq -i cscope.files && echo "=> cscope.out: $$(wc -l < cscope.files) files"; \
	else \
	    echo "=> cscope.out: skipped, cscope not found"; \
	fi

endif
