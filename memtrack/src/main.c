/*
 * main.c
 *
 *  Created on: Jan 2, 2017
 *      Author: root
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include "hashmap.h"
#include "main.h"
#include <string.h>
#include <fcntl.h>
#include <dwarf.h>
#include <libdwarf.h>
#include <errno.h>
#include <signal.h>

DLL_LOCAL int init_complete = 0;

DLL_LOCAL int did_print = 0;

DLL_LOCAL void printAlloced();

DLL_LOCAL void (*real_int_handler)(int);

DLL_LOCAL void int_handler(int arg) {
	printAlloced();
	if (real_int_handler != int_handler && real_int_handler != NULL) (*real_int_handler)(arg);
	else exit(0);
}

DLL_LOCAL void preexit() {
	printAlloced();
}

DLL_LOCAL void initfuncs() {
	if (!init_complete) {
		atexit(preexit);
		real_int_handler = signal(SIGINT, int_handler);
		init_complete = -1;
		if (!malloc_real) malloc_real = (void* (*)(size_t) ) dlsym(RTLD_NEXT, "malloc");;
		if (!calloc_real) calloc_real = (void* (*)(size_t, size_t) ) dlsym(RTLD_NEXT, "calloc");;
		if (!realloc_real) realloc_real = (void* (*)(void*, size_t) ) dlsym(RTLD_NEXT, "realloc");;
		if (!free_real) free_real = (void* (*)(void*) ) dlsym(RTLD_NEXT, "free");;
		if (!mem_alloced) mem_alloced = new_hashmap(1, 1);
		init_complete = 1;
	}
}

DLL_LOCAL struct linetable_entry {
		char* name;
		size_t line;
};

DLL_LOCAL struct alloc_proc {
		size_t size;
		char* file;
		size_t line;
};

DLL_LOCAL struct mmap_entry {
		size_t start;
		size_t end;
		char* name;
};

DLL_LOCAL char* resolveMmap(uint64_t ptr, struct hashmap* mmaps) {
	BEGIN_HASHMAP_ITERATION (mmaps)
	struct mmap_entry* mmap = value;
	if (mmap->start <= ptr && mmap->end >= ptr) return mmap->name;
	END_HASHMAP_ITERATION (mmaps)
	return NULL;
}

DLL_LOCAL void printAlloced() {
	// no point in freeing or cleaning up too much as this is called on exit
	if (did_print) return;
	did_print = 1;
	initfuncs();
	printf("Generating Line Table\n");
	struct hashmap* lineTable = new_hashmap(1, 0);
	Dwarf_Debug dbg = NULL;
	int fd = open("/proc/self/exe", O_RDONLY);
	if (fd < 0) {
		printf("Error opening our executable: %s\n", strerror(errno));
		return;
	}
	Dwarf_Error err;
	if (dwarf_init(fd, DW_DLC_READ, 0, 0, &dbg, &err) != DW_DLV_OK) {
		printf("Error initializing libdwarf!\n");
		return;
	}
	while (1) {
		Dwarf_Unsigned cu_header_length;
		Dwarf_Unsigned abbrev_offset;
		Dwarf_Unsigned next_cu_header;
		Dwarf_Half version_stamp;
		Dwarf_Half address_size;
		Dwarf_Die no_die = 0;
		Dwarf_Die cu_die;
		int ncu = dwarf_next_cu_header(dbg, &cu_header_length, &version_stamp, &abbrev_offset, &address_size, &next_cu_header, &err);
		if (ncu == DW_DLV_ERROR) {
			printf("Error reading dwarf header!\n");
			return;
		} else if (ncu == DW_DLV_NO_ENTRY) break;
		if (dwarf_siblingof(dbg, no_die, &cu_die, &err) == DW_DLV_ERROR) {
			printf("Error getting dwarf sibling!\n");
			return;
		}
		int rc = 0;
		while (1) {
			{
				Dwarf_Signed linecount = 0;
				Dwarf_Line* linebuf = NULL;
				Dwarf_Off cudie_local_offset = 0;
				Dwarf_Off dieprint_cu_goffset = 0;
				if (dwarf_die_offsets(cu_die, &dieprint_cu_goffset, &cudie_local_offset, &err) == DW_DLV_ERROR) {
					printf("Error getting dwarf offsets!\n");
					return;
				}
				int srcl_ret = dwarf_srclines(cu_die, &linebuf, &linecount, &err);
				if (srcl_ret == DW_DLV_ERROR) {
					printf("Error getting dwarf source file lines!\n");
					return;
				} else if (srcl_ret == DW_DLV_OK && linecount > 0) {
					for (Dwarf_Signed i = 0; i < linecount; i++) {
						Dwarf_Line line = linebuf[i];
						char* filename = NULL;
						if (dwarf_linesrc(line, &filename, &err) == DW_DLV_ERROR) {
							printf("Error getting dwarf filename for line!\n");
							return;
						}
						Dwarf_Addr pc = 0;
						if (dwarf_lineaddr(line, &pc, &err) != DW_DLV_OK) {
							printf("Error getting dwarf lineaddr for line!\n");
							return;
						}
						Dwarf_Unsigned lineno = 0;
						if (dwarf_lineno(line, &lineno, &err) != DW_DLV_OK) {
							printf("Error getting dwarf lineno for line!\n");
							return;
						}
						//TODO: lineoff?
						filename = strrchr(filename, '/') + 1;
						struct linetable_entry* lte = (*malloc_real)(sizeof(struct linetable_entry));
						size_t fnl = strlen(filename);
						lte->name = (*malloc_real)(fnl + 1);
						memcpy(lte->name, filename, fnl + 1);
						lte->line = lineno;
						put_hashmap(lineTable, (uint64_t) pc, lte);
					}
					dwarf_srclines_dealloc(dbg, linebuf, linecount);
				} else dwarf_srclines_dealloc(dbg, linebuf, linecount);
			}
			rc = dwarf_siblingof(dbg, cu_die, &cu_die, &err);
			if (rc == DW_DLV_ERROR) {
				printf("Error continuing down dwarf chain!\n");
				return;
			} else if (rc == DW_DLV_NO_ENTRY) break;
		}
	}
	if (dwarf_finish(dbg, &err) != DW_DLV_OK) printf("Error finishing libdwarf! Attempting to ignore...\n");
	close(fd);
	struct hashmap* proc = new_hashmap(1, 0);
	printf("Generating Usage Table\n");
	size_t unkc = 0;
	BEGIN_HASHMAP_ITERATION (mem_alloced)
	struct alloc* al = (struct alloc*) value;
	struct alloc_proc* nal = get_hashmap(proc, (uint64_t) al->mem_loc);
	if (nal == NULL) {
		nal = (*malloc_real)(sizeof(struct alloc));
		nal->size = al->size;
		struct linetable_entry* bal = NULL;
		size_t bdald = 0xFFFFFFFFFFFFFFFF;
		BEGIN_HASHMAP_ITERATION (lineTable)
		struct linetable_entry* cb = value;
		if (he->key < (uint64_t) al->mem_loc && ((uint64_t) al->mem_loc - he->key) < bdald) {
			bdald = (uint64_t) al->mem_loc - he->key; //memloc=4205289, 4205237
			bal = cb;
		}
		END_HASHMAP_ITERATION (lineTable)
		if (bal != NULL && bdald < 256) {
			nal->line = bal->line;
			nal->file = bal->name;
			put_hashmap(proc, (uint64_t) al->mem_loc, nal);
		} else {
			unkc++;
			nal->line = -1;
			nal->file = (*malloc_real)(64);
			snprintf(nal->file, 64, "<0x%lX>", (uint64_t) al->mem_loc);
			put_hashmap(proc, (uint64_t) al->mem_loc, nal);
		}
	} else {
		nal->size += al->size;
	}
	END_HASHMAP_ITERATION (mem_alloced)
	printf("Generating mmap Table\n");
	struct hashmap* mmaps = NULL;
	if (unkc > 0) {
		mmaps = new_hashmap(1, 0);
		fd = open("/proc/self/maps", O_RDONLY);
		if (fd < 0) {
			printf("Error reading /proc/self/maps.\n");
		} else {
			char* rbuf = (*malloc_real)(1025);
			size_t rbuf_size = 1024;
			size_t tr = 0;
			ssize_t r = 0;
			while ((r = read(fd, rbuf + tr, rbuf_size - tr)) > 0) {
				tr += r;
				if (rbuf_size - tr < 512) {
					rbuf_size += 1024;
					rbuf = (*realloc_real)(rbuf, rbuf_size + 1);
				}
			}
			close(fd);
			rbuf[tr] = 0;
			size_t line_count = 0;
			for (size_t i = 0; i < tr; i++) {
				if (rbuf[i] == '\n') {
					rbuf[i] = 0;
					line_count++;
				}
			}
			char* rbuf_lines[line_count];
			size_t li = 0;
			for (size_t i = 0; i < tr; i++) {
				rbuf_lines[li++] = rbuf + i;
				i += strlen(rbuf + i) + 1;
			}
			for (size_t i = 0; i < line_count; i++) {
				char* line = rbuf_lines[i];
				char* sp = strchr(line, '-');
				if (sp == NULL) continue;
				sp[0] = 0;
				sp++;
				struct mmap_entry* mme = (*malloc_real)(sizeof(struct mmap_entry));
				mme->start = strtoll(line, NULL, 16);
				mme->end = strtoll(sp, NULL, 16);
				size_t nl2 = strlen(sp);
				size_t ml = 0;
				for (size_t x = nl2 - 1; x >= 0; x--) {
					if (sp[x] == ' ') {
						if (++ml == 4) {
							sp += x;
							goto pl;
						}
					} else ml = 0;
				}
				sp = NULL;
				pl: ;
				if (sp == NULL) mme->name = NULL;
				else {
					sp += 4;
					size_t nl = strlen(sp);
					mme->name = (*malloc_real)(nl + 1);
					memcpy(mme->name, sp, nl + 1);
				}
				put_hashmap(mmaps, mme->start, mme);
			}
			(*free_real)(rbuf);
		}
	}
	char* elim = getenv("MEMCHECK_THRESHOLD");
	size_t rx = elim == NULL ? 1024 : strtol(elim, NULL, 10);
	BEGIN_HASHMAP_ITERATION (proc)
	struct alloc_proc* al = (struct alloc_proc*) value;
	if (al->size > rx) {
		if (al->line == -1) {
			char* mmap = resolveMmap(he->key, mmaps);
			if (mmap == NULL) printf("%s using %lu kB\n", al->file, al->size / 1024);
			else {
				char* ls = strrchr(mmap, '/');
				if (ls != NULL) mmap = ls + 1;
				if (strcmp(mmap, "libmemtrack")) printf("%s:%s using %lu kB\n", al->file, mmap, al->size / 1024);
			}
		} else printf("%s:%lu using %lu kB\n", al->file, al->line, al->size / 1024);
	}
	if (al->line == -1) (*free_real)(al->file);
	(*free_real)(al);
	END_HASHMAP_ITERATION (proc)
	del_hashmap(proc);
	if (mmaps != NULL) {
		BEGIN_HASHMAP_ITERATION (mmaps)
		struct mmap_entry* mmap = value;
		if (mmap->name != NULL) (*free_real)(mmap->name);
		(*free_real)(mmap);
		END_HASHMAP_ITERATION (mmaps)
		del_hashmap(mmaps);
	}
	BEGIN_HASHMAP_ITERATION (lineTable)
	struct linetable_entry* le = value;
	(*free_real)(le->name);
	(*free_real)(le);
	END_HASHMAP_ITERATION (lineTable)
	del_hashmap(lineTable);
}

void* malloc(size_t size) {
	initfuncs();
	void* ret = __builtin_return_address(0);
	void* rm = (*malloc_real)(size);
	struct alloc* al = (*malloc_real)(sizeof(struct alloc));
	al->mem_loc = ret;
	al->size = size;
	put_hashmap(mem_alloced, (uint64_t) rm, al);
	return rm;
}

void* calloc(size_t nitems, size_t size) {
	if (init_complete == -1) return NULL;
	initfuncs();
	void* ret = __builtin_return_address(0);
	void* rm = (*calloc_real)(nitems, size);
	struct alloc* al = (*malloc_real)(sizeof(struct alloc));
	al->mem_loc = ret;
	al->size = size;
	put_hashmap(mem_alloced, (uint64_t) rm, al);
	return rm;
}

void* realloc(void* ptr, size_t size) {
	initfuncs();
	void* ret = __builtin_return_address(0);
	struct alloc* al = ptr == NULL ? NULL : get_hashmap(mem_alloced, (uint64_t) ptr);
	if (ptr != NULL && al == NULL) {
		printf("[WARNING] Segfault likely, invalid realloc @ 0x%lX of size %lu on pointer 0x%lX\n", (size_t) ret, size, (size_t) ptr);
	}
	void * rm = (*realloc_real)(ptr, size);
	if (ptr == NULL || al == NULL) {
		al = (*malloc_real)(sizeof(struct alloc));
		al->mem_loc = ret;
		al->size = size;
		put_hashmap(mem_alloced, (uint64_t) rm, al);
	} else if (rm != ptr) {
		al->size = size;
		put_hashmap(mem_alloced, (uint64_t) ptr, NULL);
		put_hashmap(mem_alloced, (uint64_t) rm, al);
	} else {
		al->size = size;
	}
	//TODO: we might want to log the new return position?
	return rm;
}

void free(void* ptr) {
	initfuncs();
	void* ret = __builtin_return_address(0);
	struct alloc* al = ptr == NULL ? NULL : get_hashmap(mem_alloced, (uint64_t) ptr);
	if (ptr != NULL && al == NULL) {
		printf("[WARNING] Segfault likely, invalid free @ 0x%lX of pointer 0x%lX\n", (uint64_t) ret, (uint64_t) ptr);
	}
	(*free_real)(ptr);
	if (ptr != NULL && al != NULL) {
		put_hashmap(mem_alloced, (uint64_t) ptr, NULL);
		(*free_real)(al);
	}
}

