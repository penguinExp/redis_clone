#pragma once

#include <stddef.h>
#include <stdint.h>

// hashtable node, should be embedded into the payload
struct HNode
{
    HNode *next = NULL;
    uint64_t h_code = 0;
};

// a simple fixed-sized hashtable
struct HTab
{
    HNode **tab = NULL;
    size_t mask = 0;
    size_t size = 0;
};

// the real hashtable interface
// it uses 2 hashtables for progressive resizing
struct HMap
{
    HTab h1; // newer
    HTab h2; // older
    size_t resizing_pos = 0;
};

HNode *hm_lookup(HMap *h_map, HNode *key, bool (*eq)(HNode *, HNode *));

void hm_insert(HMap *h_map, HNode *node);

HNode *hm_pop(HMap *h_map, HNode *key, bool (*eq)(HNode *, HNode *));

size_t hm_size(HMap *h_map);

void hm_destroy(HMap *h_map);
