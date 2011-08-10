#
# Loader for l4bsd
#
# adapted from L4Linux
# arch/l4/boot/Makefile
#

L4_BUILDDIR = /home/cc/TU-Berlin/fiasco/l4-svn/src/l4/builddir

include $(L4_BUILDDIR)/l4defs.mk.inc

# Define target architecture
ifeq ($(L4_SYSTEM), $(filter amd64%, $(L4_SYSTEM)))
ARCH = amd64
endif
ifeq ($(L4_SYSTEM), $(filter arm%, $(L4_SYSTEM)))
ARCH = arm
endif
ifeq ($(L4_SYSTEM), $(filter x86%, $(L4_SYSTEM)))
ARCH = x86
endif

ifeq ($(ARCH), amd64)
BITS = 64
LDFLAGS+= -m elf_x86_64
endif
ifeq ($(ARCH), arm)
BITS = 32
LDFLAGS+= -m armelf -m armelf_linux_eabi
endif
ifeq ($(ARCH), x86)
BITS = 32
LDFLAGS+= -m elf_i386
endif

# Shall we be verbose?
ifneq ($(V), 1)
Q=@
endif

OBJS := ldr.o startup.o res.o image.o
L4OBJ := $(L4_BUILDDIR)
L4_MK_ARCH = $(ARCH)
L4_MK_API  = l4f
CC = $(L4_CC)

L4_REQUIRED_MODS := l4re-main libc_be_minimal_log_io \
                    libc_minimal libsupc++_minimal \
                    log l4re_c-util \
                    libio shmc rtc libvcpu

L4_EXT_LIBS := $(call L4_BID_PKG_CONFIG_CALL,$(L4OBJ),--libs --define-variable=libc_variant=libc,$(L4_REQUIRED_MODS))

ifneq ($(call L4_BID_PKG_CONFIG_FAILED,$(L4_EXT_LIBS)),)
$(info  Getting required libraries failed.)
$(info  L4OBJ: $(L4OBJ))
$(info  L4_REQUIRED_MODS: $(L4_REQUIRED_MODS))
$(error Aborting.)
endif

L4LIBS := $(shell $(CC) -print-file-name=libgcc_eh.a) $(L4_EXT_LIBS)

L4INC  = -I$(L4OBJ)/include/$(L4_MK_ARCH)/$(L4_MK_API) \
         -I$(L4OBJ)/include/$(L4_MK_ARCH) \
         -I$(L4OBJ)/include/$(L4_MK_API) \
         -I$(L4OBJ)/include

CDIAGFLAGS   = -Werror -Wall -Wstrict-prototypes -Wmissing-prototypes \
               -Wno-uninitialized -Wno-format -Wno-main

NOSTDINC_FLAGS = -nostdinc -isystem $(shell $(CC) -print-file-name=include)
CFLAGS	:= $(NOSTDINC_FLAGS) $(CDIAGFLAGS) -DL4API_$(L4_MK_API) \
           $(L4INC) -I$(L4OBJ)/include/uclibc \
           -Wall -fno-strict-aliasing -O2 -pipe \
	   -DARCH_$(L4_MK_ARCH) -g \
	   -I. -DL4SYS_USE_UTCB_WRAP=1
AFLAGS  := -x assembler-with-cpp -DBSD_IMAGE=\"bsd\" 

NORMAL_S = $(Q)${CC} ${AFLAGS} -c $<
NORMAL_C = $(Q)${CC} ${CFLAGS} -c $<

LINKADDR_32 = 0xa8000000
LINKADDR_64 = 0x68000000

all: l4bsd

res.o: res.c func_list.h
	$(NORMAL_C)

func_list.h: bsd
	$(Q)echo 'Found original OpenBSD kernel image "$<".'
	$(Q)objcopy -j .data.l4externals.str -O binary $< $@.tmp
	$(Q)perl -p -e 's/(.+?)\0/EF($$1)\n/g' $@.tmp > $@

image.o: image.s bsd
	$(NORMAL_S)

startup.o: startup.c
	$(NORMAL_C)

ldr.o: ldr.c
	$(NORMAL_C)

bsd:
	$(error You need to have "bsd" from your BSD kernel build directory)

l4bsd: $(OBJS)
	$(Q)$(LD) $(LDFLAGS) -Bstatic -o $@ \
	  $(L4_LIBDIRS) \
	  $(L4_CRT0_STATIC) $(OBJS) \
	  --start-group $(L4LIBS) $(L4_GCCLIB) --end-group \
	  $(L4_CRTN_STATIC) \
	  --defsym __executable_start=$(LINKADDR_$(BITS)) \
	  --defsym __L4_STACK_ADDR__=$(L4_BID_STACK_ADDR) \
	  --defsym __L4_KIP_ADDR__=$(L4_BID_KIP_ADDR) \
          --defsym __l4sys_invoke_direct=$(L4_BID_KIP_ADDR)+$(L4_BID_KIP_OFFS_SYS_INVOKE) \
          --defsym __l4sys_debugger_direct=$(L4_BID_KIP_ADDR)+$(L4_BID_KIP_OFFS_SYS_DEBUGGER) \
	  -T$(L4_LDS_stat_bin)
	  $(Q)echo '=> OpenBSD rehosted kernel "$@" is ready.'

clean:
	$(Q)rm -f l4bsd *.o func_list.h*

.PHONY: l4bsd clean all
