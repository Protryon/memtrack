/*
 * hashmap.c
 *
 *  Created on: Nov 19, 2015
 *      Author: root
 */

#include "hashmap.h"
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include "main.h"

DLL_LOCAL struct hashmap* new_hashmap(uint8_t bucket_count_bytes, uint8_t mc) {
	if (bucket_count_bytes > 2 || bucket_count_bytes < 1) return NULL;
	struct hashmap* map = (*malloc_real)(sizeof(struct hashmap));
	map->bucket_count = (size_t) pow(2, 8 * bucket_count_bytes);
	map->buckets = (*calloc_real)(1, map->bucket_count * sizeof(struct hashmap_entry*));
	map->entry_count = 0;
	map->mc = mc;
	if (mc && pthread_rwlock_init(&map->data_mutex, NULL)) { // this might call our malloc!
		(*free_real)(map->buckets);
		map->buckets = NULL;
		(*free_real)(map);
		return NULL;
	}
	return map;
}

DLL_LOCAL int del_hashmap(struct hashmap* map) {
	if (map == NULL || map->buckets == NULL) return -1;
	if (map->mc && pthread_rwlock_destroy(&map->data_mutex)) return -1; // and our free!
	for (size_t i = 0; i < map->bucket_count; i++) {
		struct hashmap_entry* head = map->buckets[i];
		while (1) {
			if (head == NULL) break;
			struct hashmap_entry* next = head->next;
			(*free_real)(head);
			head = next;
		}
	}
	(*free_real)(map->buckets);
	map->buckets = NULL;
	(*free_real)(map);
	return 0;
}

DLL_LOCAL void put_hashmap(struct hashmap* map, uint64_t key, void* value) {
	if (map->mc) pthread_rwlock_wrlock(&map->data_mutex);
	uint64_t hkey = key;
	uint8_t* hashi = (uint8_t*) &hkey;
	for (int i = 1; i < sizeof(uint64_t); i++) {
		hashi[0] ^= hashi[i];
		if (i > 1 && map->bucket_count == sizeof(uint16_t)) hashi[1] ^= hashi[i];
	}
	if (map->bucket_count == 65536) hashi[1] ^= hashi[0];
	uint16_t fhash = hashi[0];
	if (map->bucket_count == 65536) fhash |= (hashi[1] << 8);
	struct hashmap_entry* prior = map->buckets[fhash];
	struct hashmap_entry* entry = map->buckets[fhash];
	while (entry != NULL && entry->key != key) {
		prior = entry;
		entry = entry->next;
	}
	if (entry == NULL) {
		if (value != NULL) {
			map->entry_count++;
			entry = (*malloc_real)(sizeof(struct hashmap_entry));
			entry->key = key;
			entry->value = value;
			entry->next = NULL;
			if (prior != NULL && prior != entry) {
				prior->next = entry;
			} else {
				map->buckets[fhash] = entry;
			}
		}
	} else {
		if (value == NULL) {
			if (prior != entry && prior != NULL) {
				prior->next = entry->next;
			} else {
				map->buckets[fhash] = entry->next;
			}
			(*free_real)(entry);
			map->entry_count--;
		} else {
			entry->value = value;
		}
	}
	if (map->mc) pthread_rwlock_unlock(&map->data_mutex);
}

DLL_LOCAL void* get_hashmap(struct hashmap* map, uint64_t key) {
	if (map->mc) pthread_rwlock_rdlock(&map->data_mutex);
	uint64_t hkey = key;
	uint8_t* hashi = (uint8_t*) &hkey;
	for (int i = 1; i < sizeof(uint64_t); i++) {
		hashi[0] ^= hashi[i];
		if (i > 1 && map->bucket_count == 65536) hashi[1] ^= hashi[i];
	}
	if (map->bucket_count == 65536) hashi[1] ^= hashi[0];
	uint16_t fhash = hashi[0];
	if (map->bucket_count == 65536) fhash |= (hashi[1] << 8);
	struct hashmap_entry* entry = map->buckets[fhash];
	while (entry != NULL && entry->key != key) {
		entry = entry->next;
	}
	void* value = NULL;
	if (entry != NULL) {
		value = entry->value;
	}
	if (map->mc) pthread_rwlock_unlock(&map->data_mutex);
	return value;
}

DLL_LOCAL int contains_hashmap(struct hashmap* map, uint64_t key) {
	return get_hashmap(map, key) != NULL;
}
