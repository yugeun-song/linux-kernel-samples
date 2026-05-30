KVER          ?= $(shell uname -r)
KDIR          ?= /lib/modules/$(KVER)/build
ARCH          ?=
CROSS_COMPILE ?=

-include config.mk

DRIVER  := scripts/kmod.mk

SAMPLES :=

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
	@for s in $(SAMPLES); do echo "$$s"; done
