/*
 * main.h
 *
 *  Created on: Jan 2, 2017
 *      Author: root
 */

#ifndef MAIN_H_
#define MAIN_H_

#define DLL_LOCAL __attribute__ ((visibility ("hidden")))

DLL_LOCAL struct alloc {
		size_t size;
		void* mem_loc;
};

DLL_LOCAL struct hashmap* mem_alloced;

DLL_LOCAL void* (*malloc_real)(size_t);
DLL_LOCAL void* (*calloc_real)(size_t, size_t);
DLL_LOCAL void* (*realloc_real)(void*, size_t);
DLL_LOCAL void* (*free_real)(void*);

#endif /* MAIN_H_ */
