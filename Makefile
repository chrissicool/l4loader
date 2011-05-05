#
# Loader for l4bsd
#
# adapted from L4Linux
# arch/l4/boot/Makefile
#

L4_BUILDDIR = /home/cc/TU-Berlin/fiasco/l4-svn/src/l4/builddir

OBJS := ldr.o startup.o res.o image.o
L4OBJ := $(L4_BUILDDIR)
L4_MK_ARCH = x86
L4_MK_API  = l4f

# only for x86_64 aches when building x86 code
ifeq ($(shell uname -m),x86_64)
CC+= -m32
LD+= -m elf_i386
endif

include $(L4OBJ)/l4defs.mk.inc

L4_REQUIRED_MODS := stdlibs log l4re_c-util \
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
LDFLAGS := $(LDFLAGS_i386)

NORMAL_S = ${CC} ${AFLAGS} -c $<
NORMAL_C = ${CC} ${CFLAGS} -c $<

all: l4bsd

res.o: res.c func_list.h
	$(NORMAL_C)

func_list.h: bsd
	objcopy -j .data.l4externals.str -O binary $< $@.tmp
	perl -p -e 's/(.+?)\0/EF($$1)\n/g' $@.tmp > $@

image.o: image.s bsd
	$(NORMAL_S)

startup.o: startup.c
	$(NORMAL_C)

ldr.o: ldr.c
	$(NORMAL_C)

bsd:
	$(error You need to have "bsd" from your BSD kernel build directory)

l4bsd: $(OBJS)
	$(LD) $(LDFLAGS) -Bstatic -o $@ \
	  $(L4_LIBDIRS) \
	  $(L4_CRT0_STATIC) $(OBJS) \
	  --start-group $(L4LIBS) $(L4_GCCLIB) --end-group \
	  $(L4_CRTN_STATIC) \
	  -Ttext=0xa8000000 \
	  --defsym __L4_STACK_ADDR__=$(L4_BID_STACK_ADDR) \
	  --defsym __L4_KIP_ADDR__=$(L4_BID_KIP_ADDR) \
          --defsym __l4sys_invoke_direct=$(L4_BID_KIP_ADDR)+$(L4_BID_KIP_OFFS_SYS_INVOKE) \
          --defsym __l4sys_debugger_direct=$(L4_BID_KIP_ADDR)+$(L4_BID_KIP_OFFS_SYS_DEBUGGER) \
	  -T$(L4_LDS_stat_bin)

clean:
	rm -f l4bsd *.o func_list.h*

.PHONY: l4bsd clean all
