#include "sqlparser/index/table_index.h"

#include "sqlparser/common/util.h"
#include "sqlparser/index/bptree.h"
#include "sqlparser/storage/storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *table_name;
    int loaded;
    BPlusTree tree;
    int next_id;
} TableIndex;

typedef struct {
    TableIndex *items;
    int count;
    int capacity;
} TableIndexRegistry;

typedef struct {
    int id_index;
    BPlusTree *tree;
    int max_id;
} RebuildContext;

static TableIndexRegistry registry = {0};

static TableIndex *find_table_index(const char *table_name) {
    int index;

    for (index = 0; index < registry.count; index++) {
        if (strcmp(registry.items[index].table_name, table_name) == 0) {
            return &registry.items[index];
        }
    }

    return NULL;
}

static TableIndex *get_or_create_table_index(const char *table_name, char *message, size_t message_size) {
    TableIndex *existing = find_table_index(table_name);
    TableIndex *new_items;
    int new_capacity;
    TableIndex *entry;

    if (existing != NULL) {
        return existing;
    }

    if (registry.count == registry.capacity) {
        new_capacity = registry.capacity == 0 ? 4 : registry.capacity * 2;
        new_items = (TableIndex *)realloc(registry.items, (size_t)new_capacity * sizeof(TableIndex));
        if (new_items == NULL) {
            snprintf(message, message_size, "out of memory while creating table index");
            return NULL;
        }

        registry.items = new_items;
        registry.capacity = new_capacity;
    }

    entry = &registry.items[registry.count];
    memset(entry, 0, sizeof(*entry));
    entry->table_name = copy_string(table_name);
    if (entry->table_name == NULL) {
        snprintf(message, message_size, "out of memory while copying table name");
        return NULL;
    }

    bptree_init(&entry->tree);
    entry->next_id = 1;
    registry.count++;
    return entry;
}

static int rebuild_row(const StringList *fields, long row_offset, void *context, char *error, size_t error_size) {
    RebuildContext *rebuild = (RebuildContext *)context;
    int id_value;

    if (rebuild->id_index < 0 || rebuild->id_index >= fields->count) {
        snprintf(error, error_size, "missing id column while rebuilding index");
        return 0;
    }

    if (!parse_int_strict(fields->items[rebuild->id_index], &id_value)) {
        snprintf(error, error_size, "invalid integer id while rebuilding index");
        return 0;
    }

    if (!bptree_insert(rebuild->tree, id_value, row_offset, error, error_size)) {
        return 0;
    }

    if (id_value > rebuild->max_id) {
        rebuild->max_id = id_value;
    }

    return 1;
}

static int ensure_loaded(const Schema *schema, const char *data_dir, TableIndex **out_index, char *message, size_t message_size) {
    TableIndex *entry;
    RebuildContext rebuild;
    int id_index;

    id_index = schema_find_id_column(schema);
    if (id_index < 0) {
        snprintf(message, message_size, "id column is required for indexed access");
        return 0;
    }

    entry = get_or_create_table_index(schema->storage_name, message, message_size);
    if (entry == NULL) {
        return 0;
    }

    if (!entry->loaded) {
        bptree_free(&entry->tree);
        bptree_init(&entry->tree);
        rebuild.id_index = id_index;
        rebuild.tree = &entry->tree;
        rebuild.max_id = 0;

        if (!scan_rows_csv(data_dir, schema->storage_name, rebuild_row, &rebuild, message, message_size)) {
            bptree_free(&entry->tree);
            bptree_init(&entry->tree);
            entry->loaded = 0;
            entry->next_id = 1;
            return 0;
        }

        entry->loaded = 1;
        entry->next_id = rebuild.max_id + 1;
    }

    *out_index = entry;
    return 1;
}

void table_index_registry_reset(void) {
    int index;

    for (index = 0; index < registry.count; index++) {
        free(registry.items[index].table_name);
        bptree_free(&registry.items[index].tree);
    }

    free(registry.items);
    registry.items = NULL;
    registry.count = 0;
    registry.capacity = 0;
}

void table_index_invalidate(const char *table_name) {
    TableIndex *entry = find_table_index(table_name);
    if (entry == NULL) {
        return;
    }

    bptree_free(&entry->tree);
    bptree_init(&entry->tree);
    entry->loaded = 0;
    entry->next_id = 1;
}

int table_index_is_loaded(const char *table_name) {
    TableIndex *entry = find_table_index(table_name);
    return entry != NULL && entry->loaded;
}

int table_index_get_next_id(const Schema *schema, const char *data_dir, int *next_id, char *message, size_t message_size) {
    TableIndex *entry;

    if (!ensure_loaded(schema, data_dir, &entry, message, message_size)) {
        return 0;
    }

    *next_id = entry->next_id;
    return 1;
}

int table_index_register_row(const Schema *schema, const char *data_dir, int id, long row_offset, char *message, size_t message_size) {
    TableIndex *entry;

    if (!ensure_loaded(schema, data_dir, &entry, message, message_size)) {
        return 0;
    }

    if (!bptree_insert(&entry->tree, id, row_offset, message, message_size)) {
        return 0;
    }

    if (id >= entry->next_id) {
        entry->next_id = id + 1;
    }

    return 1;
}

TableIndexLookupResult table_index_find_row(const Schema *schema, const char *data_dir, int id) {
    TableIndexLookupResult result = {0};
    TableIndex *entry;

    if (!ensure_loaded(schema, data_dir, &entry, result.message, sizeof(result.message))) {
        return result;
    }

    result.ok = 1;
    result.found = bptree_search(&entry->tree, id, &result.row_offset);
    return result;
}
