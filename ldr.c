
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <l4/util/elf.h>
#include <l4/util/util.h>
#include <l4/log/log.h>
#include <l4/re/c/mem_alloc.h>
#include <l4/re/c/rm.h>
#include <l4/re/c/util/cap_alloc.h>

#include <l4/sys/compiler.h>
#include <l4/sys/kdebug.h>
#include <l4/sys/vcpu.h>

extern const char image_bsd_start[];

struct shared_data {
	unsigned long (*external_resolver)(void);
	L4_CV l4_utcb_t *(*l4lx_utcb)(void);
	void *l4re_global_env;
	void *kip;
};
struct shared_data exchg;

unsigned long __l4_external_resolver(void);

L4_CV l4_utcb_t *l4_utcb_wrap(void)
{
	if (exchg.l4lx_utcb)
		return exchg.l4lx_utcb();
	return l4_utcb_direct();
}


/*
 * exit function on errors inside the VM.
 */
void l4x_external_exit(int code);
void l4x_external_exit(int code)
{
	_exit(code);
}

void do_resolve_error(const char *funcname);
void do_resolve_error(const char *funcname)
{
	LOG_printf("Symbol '%s' not found\n", funcname);
	enter_kdebug("Symbol not found!");
}

/*
 * XXX hshoexer: Aren't we leaking caps and dataspaces in error paths?
 */
int main(int argc, char **argv)
{
	ElfW(Ehdr) *ehdr = (void *)image_bsd_start;
	ElfW(Ehdr) *elfp;
	ElfW(Shdr) *shp, *shpp;
	ElfW(Off) off;
	l4_addr_t minp = ~0, maxp = 0, pos = 0;
	size_t sz;
	int i, j, havesyms;
	int (*entry)(int, char **);

	if (!l4util_elf_check_magic(ehdr)
	    || !l4util_elf_check_arch(ehdr)) {
		printf("ldr: Invalid vmlinux binary (No ELF)\n");
		return 1;
	}

	printf("\033[34;1m======>  BSD/Ldr ... <========\033[0m\n");

	/* some Elf debugging infos */
	printf("Got an ELF: headersize=%08x, PHs=%08x\n",
		ehdr->e_ehsize, ehdr->e_phnum);

	for (i = 0; i < ehdr->e_phnum; ++i) {
		int r;
		l4_addr_t map_addr;
		unsigned long map_size;
		unsigned flags;
		l4re_ds_t ds, new_ds;
		l4_addr_t offset;

		ElfW(Phdr) *ph = (ElfW(Phdr)*)((l4_addr_t)l4util_elf_phdr(ehdr)
		                               + i * ehdr->e_phentsize);
		printf("PH %2d (t: %8d) offs=%08x vaddr=%08x vend=%08x\n"
		       "                    f_sz=%08x memsz=%08x flgs=%c%c%c\n",
		       i, ph->p_type, ph->p_offset, ph->p_vaddr,
		       ph->p_vaddr + ph->p_memsz,
		       ph->p_filesz, ph->p_memsz,
		       ph->p_flags & PF_R ? 'r' : '-',
		       ph->p_flags & PF_W ? 'w' : '-',
		       ph->p_flags & PF_X ? 'x' : '-');
		if (ph->p_type != PT_LOAD)
			continue;

		if (ph->p_vaddr & ~L4_PAGEMASK) {
			printf("ldr: unaligned section\n");
			continue;
		}

		pos = ph->p_paddr;
		if (minp > pos)
			minp = pos;
		pos += ph->p_filesz;
		if (maxp < pos)
			maxp = pos;

		printf("maxp 0x%lx pos 0x%lx\n", (unsigned long)maxp,
		    (unsigned long)pos);
		
		if (ph->p_filesz < ph->p_memsz) {

			ds = l4re_util_cap_alloc();
			if (l4_is_invalid_cap(ds)) {
				printf("ldr: out of caps\n");
				return 1;
			}

			if (l4re_ma_alloc(ph->p_memsz, ds,
			                  L4RE_MA_CONTINUOUS | L4RE_MA_PINNED)) {
				printf("ldr: could not allocate memory\n");
				return 1;
			}

			map_addr = ph->p_vaddr;
			if (l4re_rm_attach((void **)&map_addr,
			                    ph->p_memsz, L4RE_RM_EAGER_MAP,
			                    ds, 0, 0)) {
				printf("ldr: failed attaching memory\n");
				return 1;
			}
			printf("mapped %zu bytes at 0x%lx (0x%lx)\n",
			    ph->p_memsz, (unsigned long)ph->p_vaddr,
			    (unsigned long)ph->p_vaddr + ph->p_memsz);
#if 1
			printf("Copy segment: %p -> %p (%x), zero out %p (%x)\n",
			(void *)ph->p_vaddr,
			       (char *)image_bsd_start + ph->p_offset,
			       ph->p_filesz,
			       (void *)ph->p_vaddr + ph->p_filesz,
			       ph->p_memsz - ph->p_filesz);
#endif
			memcpy((void *)ph->p_vaddr,
			       (char *)image_bsd_start + ph->p_offset,
			       ph->p_filesz);
			memset((void *)ph->p_vaddr + ph->p_filesz, 0,
			       ph->p_memsz - ph->p_filesz);
			pos += ph->p_memsz - ph->p_filesz;
			if (maxp < pos)
				maxp = pos;
			continue;
		}

		new_ds = l4re_util_cap_alloc();
		if (l4_is_invalid_cap(new_ds)) {
			printf("ldr: out of caps\n");
			return 1;
		}

		if (l4re_ma_alloc(ph->p_memsz, new_ds,
		                  L4RE_MA_CONTINUOUS | L4RE_MA_PINNED)) {
			printf("ldr: could not allocate memory\n");
			return 1;
		}

		/* We just need the dataspace and offset */
		map_addr = (l4_addr_t)image_bsd_start + ph->p_offset;
		map_size = ph->p_memsz;
		r = l4re_rm_find(&map_addr, &map_size, &offset, &flags, &ds);
		offset += ((l4_addr_t)image_bsd_start + ph->p_offset) - map_addr;
		if (r) {
			printf("ldr: Failed lookup %p (%p + %lx): %d\n",
			       (char *)image_bsd_start + ph->p_offset,
			       (char *)image_bsd_start,
			       (unsigned long)ph->p_offset, r);
			return 1;
		}

		r = l4re_ds_copy_in(new_ds, 0, ds, offset, ph->p_memsz);
		if (r)
			printf("l4dm_mem_copyin failed\n");

		map_addr = ph->p_vaddr;
		if (l4re_rm_attach((void **)&map_addr,
		                    ph->p_memsz, L4RE_RM_EAGER_MAP,
		                    new_ds, 0, 0)) {
			printf("ldr: failed to attach section\n");
			return 1;
		}
		printf("mapped %zu bytes at 0x%lx (0x%lx)\n", ph->p_memsz,
		    map_addr, (unsigned long)map_addr + ph->p_memsz);
	}

	printf("maxp 0x%lx\n", (unsigned long)maxp);

	/* 
	 * Copy ELF and section headers.
	 */
#define roundup(x, y)	((((x)+((y)-1))/(y))*(y))

	/* Will store the ELF header at elfp. */
	maxp = roundup(maxp, 4096/*sizeof(ElfW(Addr))*/);
	elfp = (ElfW(Ehdr) *)maxp;
	sz = ehdr->e_shnum * sizeof(ElfW(Shdr)) + sizeof(ElfW(Ehdr));
	maxp += sizeof(ElfW(Ehdr));

	printf("elfp %p maxp 0x%lx\n", elfp, (unsigned long)maxp);

	shpp = (ElfW(Shdr)*)maxp;
	maxp += sz;
	maxp = roundup(maxp, 4096);

	printf("shpp 0x%lx sz %zu maxp 0x%lx\n", (unsigned long)shpp, sz,
	    (unsigned long)maxp);

	l4re_ds_t ds;
	l4_addr_t map_addr;

	/* Create chunk of memory for section headers. */
	ds = l4re_util_cap_alloc();
	if (l4_is_invalid_cap(ds)) {
		printf("ldr: out of caps\n");
		return 1;
	}

	if (l4re_ma_alloc(sz, ds, L4RE_MA_CONTINUOUS | L4RE_MA_PINNED)) {
		printf("ldr: could not allocate memory\n");
		return 1;
	}

	map_addr = (l4_addr_t)shpp;
	if ((i = l4re_rm_attach((void **)&map_addr, sz,  L4RE_RM_EAGER_MAP, ds,
	    0, 0)) != 0) {
		printf("ldr: failed attaching memory %d\n", i);
		return 1;
	}
	printf("mapped %zu bytes at 0x%lx (0x%lx)\n", sz, map_addr,
	    (unsigned long)map_addr + sz);

	off = roundup((sizeof(ElfW(Ehdr)) + sz), sizeof(ElfW(Addr)));

	printf("off 0x%lx\n", (unsigned long)off);

	shp = (ElfW(Shdr)*)((l4_addr_t)image_bsd_start + ehdr->e_shoff);
	for (havesyms = i = 0; i < ehdr->e_shnum; i++) {
		printf("shp[%d] 0x%lx\n", i, (unsigned long)&shp[i]);
		if (shp[i].sh_type == SHT_SYMTAB)
			havesyms = 1;
	}

	for (j = i = 0; i < ehdr->e_shnum; i++) {
		if (shp[i].sh_type == SHT_SYMTAB ||
		    shp[i].sh_type == SHT_STRTAB) {
			if (!havesyms)
				continue;

			printf("loading section \"%s\" size %ld to 0x%lx\n",
			    shp[i].sh_type == SHT_SYMTAB ? "symbols" :
			    "strings", (unsigned long)shp[i].sh_size,
			    (unsigned long)maxp);
		} else
			continue;

		/* Create chunk of memory at maxp. */
		ds = l4re_util_cap_alloc();
		if (l4_is_invalid_cap(ds)) {
			printf("ldr: out of caps\n");
			return 1;
		}

		if (l4re_ma_alloc(shp[i].sh_size, ds,
		    L4RE_MA_CONTINUOUS | L4RE_MA_PINNED)) {
			printf("ldr: could not allocate memory\n");
			return 1;
		}

		map_addr = maxp;
		if (l4re_rm_attach((void **)&map_addr, shp[i].sh_size,
		    L4RE_RM_EAGER_MAP, ds, 0, 0)) {
			printf("ldr: failed attaching memory\n");
			return 1;
		}
		printf("mapped %zu bytes at 0x%lx (0x%lx)\n", shp[i].sh_size,
		    map_addr, (unsigned long)map_addr + shp[i].sh_size);
		printf("size %zu offset 0x%lx source 0x%lx image 0x%lx\n",
		    shp[i].sh_size, (unsigned long)shp[i].sh_offset,
		    (l4_addr_t)image_bsd_start + shp[i].sh_offset,
		    (l4_addr_t)image_bsd_start);

		/* Pull in section. */
		memcpy((void *)maxp, (void *)((l4_addr_t)image_bsd_start +
		    shp[i].sh_offset), shp[i].sh_size);

		maxp += roundup(shp[i].sh_size, sizeof(ElfW(Addr)));
		maxp = roundup(maxp, 4096);

		printf("section %d: %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx\n",
		    i,
		    (unsigned long)shp[i].sh_name,
		    (unsigned long)shp[i].sh_type,
		    (unsigned long)shp[i].sh_flags,
		    (unsigned long)shp[i].sh_addr,
		    (unsigned long)shp[i].sh_offset,
		    (unsigned long)shp[i].sh_size,
		    (unsigned long)shp[i].sh_link,
		    (unsigned long)shp[i].sh_info,
		    (unsigned long)shp[i].sh_addralign,
		    (unsigned long)shp[i].sh_entsize);

		/* Patch in new values. */
		shpp[j] = shp[i];
		shpp[j].sh_offset = off;
		j++;
		off += roundup(shp[i].sh_size, sizeof(ElfW(Addr)));

		printf("maxp 0x%lx off %lx\n", (unsigned long)maxp,
		    (unsigned long)off);
	}

	for (i = 0; i < j; i++) {
		printf("section %d: %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx\n",
		    i,
		    (unsigned long)shpp[i].sh_name,
		    (unsigned long)shpp[i].sh_type,
		    (unsigned long)shpp[i].sh_flags,
		    (unsigned long)shpp[i].sh_addr,
		    (unsigned long)shpp[i].sh_offset,
		    (unsigned long)shpp[i].sh_size,
		    (unsigned long)shpp[i].sh_link,
		    (unsigned long)shpp[i].sh_info,
		    (unsigned long)shpp[i].sh_addralign,
		    (unsigned long)shpp[i].sh_entsize);
	}

	/* Patch ELF header. */
	*elfp = *ehdr;
	elfp->e_phoff = 0;
	elfp->e_shoff = sizeof(ElfW(Ehdr));
	elfp->e_phentsize = 0;
	elfp->e_phnum = 0;

	printf("ELF: %hx %hx %lx %lx %lx %lx %lx %hx %hx %hx %hx %hx %hx\n",
	    ehdr->e_type, ehdr->e_machine,
	    (unsigned long)ehdr->e_version, (unsigned long)ehdr->e_entry,
	    (unsigned long)ehdr->e_phoff, (unsigned long)ehdr->e_shoff,
	    (unsigned long)ehdr->e_flags, ehdr->e_ehsize, ehdr->e_phentsize,
	    ehdr->e_phnum, ehdr->e_shentsize, ehdr->e_shnum, ehdr->e_shstrndx);
	printf("ELF: %hx %hx %lx %lx %lx %lx %lx %hx %hx %hx %hx %hx %hx\n",
	    elfp->e_type, elfp->e_machine,
	    (unsigned long)elfp->e_version, (unsigned long)elfp->e_entry,
	    (unsigned long)elfp->e_phoff, (unsigned long)elfp->e_shoff,
	    (unsigned long)elfp->e_flags, elfp->e_ehsize, elfp->e_phentsize,
	    elfp->e_phnum, elfp->e_shentsize, elfp->e_shnum, elfp->e_shstrndx);

	exchg.external_resolver = __l4_external_resolver;
	exchg.l4re_global_env  = l4re_global_env;
	exchg.kip              = l4re_kip();
	printf("External resolver is at %p\n", __l4_external_resolver);
	entry = (void *)ehdr->e_entry;
	printf("Starting binary at %p, argc=%d argv0=%s\n", entry, argc, *argv);
	/*
	printf("Hexdump of binary: %02x %02x %02x %02x %02x\n",
		*((char *)entry + 0), *((char *)entry + 1), *((char *)entry + 2),
		*((char *)entry + 3), *((char *)entry + 4));
	*/
#ifdef ARCH_arm
	{
		register unsigned long _argc  asm("r0") = argc;
		register unsigned long _argv  asm("r1") = (unsigned long)argv;
		register unsigned long _exchg asm("r2") = (unsigned long)&exchg;
		register unsigned long _entry asm("r3") = (unsigned long)entry;
		asm volatile("mov lr, pc   \n"
		             "mov pc, r3   \n"
			     "mov %0, r0   \n"
			     : "=r" (i)
			     : "r" (_argv),
			       "r" (_argc),
			       "r" (_exchg),
			       "r" (_entry)
			     : "memory");
	}
#else
	asm volatile("push %[argv]\n"
	             "push %[argc]\n"
	             "mov  %[exchg], %%esi\n"
	             "call  *%[entry]\n"
	             "pop %[argc]\n"
	             "pop %[argv]\n"
		     : "=a" (i)
		     : [argv] "r" (argv),
		       [argc] "r" (argc),
		       [exchg] "r" (&exchg),
		       [entry] "r" (entry)
		     : "memory");
#endif

	return i;
}
