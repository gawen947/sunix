/* File: htable.c

   Copyright (c) 2011 David Hauweele <david@hauweele.net>
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the University nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
   OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
   HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
   OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
   SUCH DAMAGE. */

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "htable.h"

#define IDX(hash, size) hash & (size - 1)

struct entry {
  const void *key;
  void *data;

  struct entry *next;
};

struct htable {
  uint32_t (*hash)(const void *);
  bool (*compare)(const void *, const void *);
  void (*destroy)(void *);

  unsigned int nbuckets;

  struct entry *buckets[];
};

htable_t ht_create(unsigned int nbuckets,
                   uint32_t (*hash)(const void *),
                   bool (*compare)(const void *, const void *),
                   void (*destroy)(void *))
{
  struct htable *ht = malloc(sizeof(struct htable) +            \
                             sizeof(struct entry) * nbuckets);

  if(!ht)
    return NULL;
  memset(ht, 0, sizeof(struct htable) + sizeof(struct entry *) * nbuckets);

  ht->nbuckets = nbuckets;

  ht->hash    = hash;
  ht->compare = compare;
  ht->destroy = destroy;

  return ht;
}

void * ht_search(htable_t ht, const void *key, void *data)
{
  struct entry *entry;
  uint32_t index    = IDX(ht->hash(key), ht->nbuckets);

  for(entry = ht->buckets[index] ; entry ; entry = entry->next) {
    if(ht->compare(entry->key, key)) {
      if(data) {
        ht->destroy(entry->data);
        entry->key  = key;
        entry->data = data;
      }

      return entry->data;
    }
  }

  if(data) {
    struct entry *new = malloc(sizeof(struct entry));

    new->key  = key;
    new->data = data;
    new->next = ht->buckets[index];

    ht->buckets[index] = new;

    return data;
  }

  return NULL;
}

void * ht_lookup(htable_t ht, const void *key,
                 void *(retrieve)(const void *, void *),
                 void *optarg)
{
  struct entry *entry;
  uint32_t index    = IDX(ht->hash(key), ht->nbuckets);

  for(entry = ht->buckets[index] ; entry ; entry = entry->next)
    if(ht->compare(entry->key, key))
      return entry->data;

  entry = malloc(sizeof(struct entry));
  entry->key  = key;
  entry->data = retrieve(key, optarg);
  entry->next = ht->buckets[index];

  ht->buckets[index] = entry;

  return entry->data;
}

void ht_walk(htable_t ht, void (*action)(void *))
{
  unsigned int i;

  for(i = 0 ; i < ht->nbuckets ; i++) {
    struct entry *entry;

    for(entry = ht->buckets[i] ; entry ; entry = entry->next)
      action(entry->data);
  }
}

void ht_delete(htable_t ht, const void *key)
{
  struct entry *entry;
  struct entry *prev = NULL;
  uint32_t index     = IDX(ht->hash(key), ht->nbuckets);

  for(entry = ht->buckets[index], prev = NULL ;
      entry ;
      prev = entry, entry = entry->next)
    if(ht->compare(entry->key, key))
      break;

  if(!entry)
    return;

  if(!prev)
    ht->buckets[index] = entry->next;
  else
    prev->next = entry->next;

  ht->destroy(entry->data);
  free(entry);
}

void ht_destroy(htable_t ht)
{
  unsigned int i;

  for(i = 0 ; i < ht->nbuckets ; i++) {
    struct entry *entry = ht->buckets[i];

    while(entry) {
      struct entry *f = entry;

      entry = entry->next;
      ht->destroy(f->data);
      free(f);
    }
  }

  free(ht);
}

