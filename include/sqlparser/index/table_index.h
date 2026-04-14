#ifndef SQLPARSER_INDEX_TABLE_INDEX_H
#define SQLPARSER_INDEX_TABLE_INDEX_H

#include "sqlparser/storage/schema.h"

#include <stddef.h>

typedef struct {
    int ok;
    int found;
    long row_offset;
    char message[256];
} TableIndexLookupResult;

void table_index_registry_reset(void);
void table_index_invalidate(const char *table_name);
int table_index_is_loaded(const char *table_name);
int table_index_get_next_id(const Schema *schema, const char *data_dir, int *next_id, char *message, size_t message_size);
int table_index_register_row(const Schema *schema, const char *data_dir, int id, long row_offset, char *message, size_t message_size);
TableIndexLookupResult table_index_find_row(const Schema *schema, const char *data_dir, int id);

#endif
