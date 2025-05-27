#
# Makefile to build AmigaOS/68k tools using Bebbo's GCC cross-compiler.
#

BEC_PROG  := bec
BEC_TOOL  := bectool

ALL_PROGS    := $(BEC_PROG)
#ALL_PROGS    := $(BEC_PROG) $(BEC_TOOL)

OBJDIR       := objs
CRC32_C      := ../fw/crc32.c
CTRL_SRCS   := bec.c $(CRC32_C)
CTRL_HDRS   := ../fw/crc32.h
APTEST_SRCS   := bectest.c $(CRC32_C)
APTEST_HDRS   := ../fw/crc32.h

CC      := m68k-amigaos-gcc
STRIP   := m68k-amigaos-strip
CFLAGS  := -Wall -Wextra -Wno-pointer-sign -Wno-format -Wno-strict-aliasing
CFLAGS  += -Wno-sign-compare -fomit-frame-pointer -I../fw -mcpu=68060
LDFLAGS_CTRL = -Xlinker -Map=$(OBJDIR)/$@.map -Wa,-a > $(OBJDIR)/$@_prog.lst -mcrt=clib2 -lgcc -lc -lamiga
LDFLAGS_APTEST = -Xlinker -Map=$(OBJDIR)/$@.map -Wa,-a > $(OBJDIR)/$@_prog.lst -mcrt=clib2 -lgcc -lc -lamiga

NOW  := $(shell date +%s)
ifeq ($(OS),Darwin)
DATE := $(shell date -j -f %s $(NOW)  '+%Y-%m-%d')
TIME := $(shell date -j -f %s $(NOW)  '+%H:%M:%S')
else
DATE := $(shell date -d "@$(NOW)" '+%Y-%m-%d')
TIME := $(shell date -d "@$(NOW)" '+%H:%M:%S')
endif
CFLAGS += -DBUILD_DATE=\"$(DATE)\" -DBUILD_TIME=\"$(TIME)\"

VERSION := $(shell awk '/Version/{print $$2}' ../fw/version.c)
CFLAGS += -DVERSION=\"$(VERSION)\"
PROGVER := bec_$(VERSION)

NDK_PATH := /opt/amiga/m68k-amigaos/ndk-include
VASM     := vasmm68k_mot

CFLAGS  += -Os
QUIET   := @

#LDFLAGS += -g
#CFLAGS  += -g

# If verbose is specified with no other targets, then build everything
ifeq ($(MAKECMDGOALS),verbose)
verbose: all
endif
ifeq (,$(filter verbose timed, $(MAKECMDGOALS)))
QUIET   := @
else
QUIET   :=
endif

ifeq (, $(shell which $(CC) 2>/dev/null ))
$(error "No $(CC) in PATH: maybe do PATH=$$PATH:/opt/amiga13/bin")
endif

all: $(ALL_PROGS)
	@:

gdb:
	m68k-amigaos-gdb $(BEC_PROG)

TEST :=
define DEPEND_SRC
# The following line creates a rule for an object file to depend on a
# given source file.
$(patsubst %,$(2)/%,$(filter-out $(2)/%,$(basename $(notdir $(1))).o)) $(filter $(2)/%,$(basename $(1)).o): $(1)
# The following line adds that object to macro containing the list of objects
$(3) += $(patsubst %,$(2)/%,$(filter-out $(2)/%,$(basename $(notdir $(1))).o))
endef

$(foreach SRCFILE,$(CTRL_SRCS),$(eval $(call DEPEND_SRC,$(SRCFILE),$(OBJDIR),CTRL_OBJS)))
$(foreach SRCFILE,$(APTEST_SRCS),$(eval $(call DEPEND_SRC,$(SRCFILE),$(OBJDIR),APTEST_OBJS)))

OBJS := $(sort $(CTRL_OBJS) $(APTEST_OBJS))

$(CTRL_OBJS): $(CTRL_HDRS)
$(APTEST_OBJS): $(APTEST_HDRS)

$(OBJS): Makefile | $(OBJDIR)
	@echo Building $@
	$(QUIET)$(CC) $(CFLAGS) -c $(filter %.c,$^) -Wa,-a,-ad >$(@:.o=.lst) -o $@

$(BEC_PROG): $(CTRL_OBJS)
	@echo Building $@
	$(QUIET)$(CC) $^ $(LDFLAGS_CTRL) -o $@

$(BEC_TOOL): $(APTEST_OBJS)
	@echo Building $@
	$(QUIET)$(CC) $^ $(LDFLAGS_APTEST) -o $@

$(OBJDIR):
	mkdir -p $@

ZIPFILE := $(PROGVER).zip
LHAFILE := $(PROGVER).lha
DISK	:= $(PROGVER).adf

zip:
	@echo Building $(ZIPFILE)
	$(QUIET)rm -rf $(ZIPFILE) $(PROGVER)
	$(QUIET)mkdir $(PROGVER)
	$(QUIET)cp -p $(ALL_PROGS) $(PROGVER)/
	$(QUIET)zip -rq $(ZIPFILE) $(PROGVER)
	$(QUIET)rm -rf $(PROGVER)

lha:
	@echo Building $(LHAFILE)
	$(QUIET)rm -rf $(LHAFILE) $(PROGVER)
	$(QUIET)mkdir $(PROGVER)
	$(QUIET)cp -p $(ALL_PROGS) $(PROGVER)/
	$(QUIET)lha -aq2 $(LHAFILE) $(PROGVER)
	$(QUIET)rm -rf $(PROGVER)

adf:
	xdftool $(DISK) format "$(PROGVER)"
	xdftool $(DISK) makedir c
	xdftool $(DISK) write bec c/bec
	xdftool $(DISK) boot install

clean clean-all:
	@echo Cleaning
	@rm -rf $(OBJS) $(OBJDIR)

FLINT_FILE=flexelint.lnt
flint:
	flexelint -v -w3 -I/opt/amiga/m68k-amigaos/ndk-include -I/opt/amiga/m68k-amigaos/sys-include -I/opt/amiga/m68k-amigaos/clib2/include flexelint.lnt $(CTRL_SRCS)
