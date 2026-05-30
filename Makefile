KVER          ?= $(shell uname -r)
KDIR          ?= /lib/modules/$(KVER)/build
ARCH          ?=
CROSS_COMPILE ?=

-include config.mk

DRIVER  := scripts/kmod.mk
SAMPLES := $(sort $(patsubst ./%/,%,$(dir $(shell find . \( -path ./scripts -o -path ./.git -o -name user -o -name '*.user.c' \) -prune -o -name '*.c' -print))))

PASS := KVER='$(KVER)' KDIR='$(KDIR)'
ifneq ($(ARCH),)
PASS += ARCH='$(ARCH)'
endif
ifneq ($(CROSS_COMPILE),)
PASS += CROSS_COMPILE='$(CROSS_COMPILE)'
endif

SUBMAKE = $(MAKE) -f $(DRIVER) $(PASS)

.PHONY: all clean list $(SAMPLES)

all: $(SAMPLES)

$(SAMPLES):
	$(SUBMAKE) build SAMPLE=$@

clean:
	@for s in $(SAMPLES); do $(SUBMAKE) clean SAMPLE=$$s; done

list:
	@printf '%s\n' $(SAMPLES)
