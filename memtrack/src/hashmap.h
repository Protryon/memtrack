/*
 * hashmap.h
 *
 *  Created on: Nov 19, 2015
 *      Author: root
 */

#ifndef HASHMAP_H_
#define HASHMAP_H_

#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include "main.h"

DLL_LOCAL struct hashmap {
		struct hashmap_entry** buckets;
		size_t bucket_count;
		size_t entry_count;
		uint8_t mc;
		pthread_rwlock_t data_mutex;
};

DLL_LOCAL struct hashmap_entry {
		uint64_t key;
		void* value;
		struct hashmap_entry* next;
};

DLL_LOCAL struct hashmap* new_hashmap(uint8_t bucket_count_bytes, uint8_t mc); // bucket_count_bytes must be 1 or 2.

DLL_LOCAL int del_hashmap(struct hashmap* map);

DLL_LOCAL void put_hashmap(struct hashmap* map, uint64_t key, void* value);

DLL_LOCAL void* get_hashmap(struct hashmap* map, uint64_t key);

DLL_LOCAL int rem_hashmap(struct hashmap* map, uint64_t data);

DLL_LOCAL int contains_hashmap(struct hashmap* map, uint64_t data);

#define BEGIN_HASHMAP_ITERATION(hashmap) if(hashmap->mc) pthread_rwlock_rdlock(&hashmap->data_mutex); for (size_t _hmi = 0; _hmi < hashmap->bucket_count; _hmi++) {struct hashmap_entry* he = hashmap->buckets[_hmi];int _s = 1;struct hashmap_entry* nhe = NULL;while (he != NULL) {if(!_s)he = nhe;else _s=0;if(he == NULL)break;nhe = he->next;void* value = he->value;
#define END_HASHMAP_ITERATION(hashmap)	}}if(hashmap->mc) pthread_rwlock_unlock(&hashmap->data_mutex);
#define BREAK_HASHMAP_ITERATION(hashmap)	if(hashmap->mc) pthread_rwlock_unlock(&hashmap->data_mutex);

#endif /* HASHMAP_H_ */
