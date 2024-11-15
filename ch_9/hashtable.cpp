#include <assert.h>
#include <stdlib.h>
#include "hashtable.h"

const size_t k_max_load_factor = 8;
const size_t k_resizing_work = 128;

// n must be power for 2
static void h_init(HTab *h_tab, size_t n)
{
    assert(n > 0 && ((n - 1) & n) == 0);

    h_tab->tab = (HNode **)calloc(sizeof(HNode *), n);
    h_tab->mask = n - 1;
    h_tab->size = 0;
}

// hashtable insertion
static void h_insert(HTab *h_tab, HNode *node)
{
    size_t pos = node->h_code & h_tab->mask; // slot index
    HNode *next = h_tab->tab[pos];           // prepend the list

    node->next = next;
    h_tab->tab[pos] = node;
    h_tab->size++;
}

// hashtable look up subroutine.
//
// Pay attention to the return value. It return the address of
// the parent pointer that owns the target node,
// which can be used to delete the target node.
static HNode **h_lookup(HTab *h_tab, HNode *key, bool (*eq)(HNode *, HNode *))
{
    if (!h_tab->tab)
    {
        return NULL;
    }

    size_t pos = key->h_code & h_tab->mask;
    HNode **from = &h_tab->tab[pos]; // incoming pointer to the result

    for (HNode *cur; (cur = *from) != NULL; from = &cur->next)
    {
        if (cur->h_code == key->h_code && eq(cur, key))
        {
            return from;
        }
    };

    return NULL;
}

// remove the node from the chain
static HNode *h_detach(HTab *h_tab, HNode **from)
{
    HNode *node = *from;
    *from = node->next;
    h_tab->size--;

    return node;
}

static void hm_help_resizing(HMap *h_map)
{
    size_t n_work = 0;

    while (n_work < k_resizing_work && h_map->h2.size > 0)
    {
        // scan for nodes from [h2] and move them to [h1]
        HNode **from = &h_map->h2.tab[h_map->resizing_pos];

        if (!*from)
        {
            h_map->resizing_pos++;
            continue;
        }

        h_insert(&h_map->h1, h_detach(&h_map->h2, from));
        n_work++;
    }

    if (h_map->h2.size == 0 && h_map->h2.tab)
    {
        // done
        free(h_map->h2.tab);
        h_map->h2 = HTab{};
    }
}

static void hm_start_resizing(HMap *h_map)
{
    assert(h_map->h2.tab == NULL);

    // create a bigger hashtable and swap them
    h_map->h2 = h_map->h1;

    h_init(&h_map->h1, (h_map->h1.mask + 1) * 2);
    h_map->resizing_pos = 0;
}

HNode *hm_lookup(HMap *h_map, HNode *key, bool (*eq)(HNode *, HNode *))
{

    hm_help_resizing(h_map);

    HNode **from = h_lookup(&h_map->h1, key, eq);
    from = from ? from : h_lookup(&h_map->h2, key, eq);

    return from ? *from : NULL;
}

void hm_insert(HMap *h_map, HNode *node)
{
    if (!h_map->h1.tab)
    {
        h_init(&h_map->h1, 4); // 1. Init the table if it's empty
    }

    h_insert(&h_map->h1, node); // 2. Insert the key into the newer table

    // 3. Check the load factor
    if (!h_map->h2.tab)
    {
        size_t load_factor = h_map->h1.size / (h_map->h1.mask + 1);

        if (load_factor >= k_max_load_factor)
        {
            hm_start_resizing(h_map); // create a larger table
        }
    }

    hm_help_resizing(h_map); // 4. Move some keys into the newer table.
}

HNode *hm_pop(HMap *h_map, HNode *key, bool (*eq)(HNode *, HNode *))
{
    hm_help_resizing(h_map);

    if (HNode **from = h_lookup(&h_map->h1, key, eq))
    {
        return h_detach(&h_map->h1, from);
    }

    if (HNode **from = h_lookup(&h_map->h2, key, eq))
    {
        return h_detach(&h_map->h2, from);
    }

    return NULL;
}

size_t hm_size(HMap *h_map)
{
    return h_map->h1.size + h_map->h2.size;
}

void hm_destroy(HMap *h_map)
{
    free(h_map->h1.tab);
    free(h_map->h2.tab);

    *h_map = HMap{};
}