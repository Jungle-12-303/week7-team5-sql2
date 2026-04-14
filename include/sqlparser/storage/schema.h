#ifndef SCHEMA_H
#define SCHEMA_H

#include "sqlparser/common/util.h"

typedef struct {
    char *table_name;
    char *storage_name;
    StringList columns;
} Schema;

typedef struct {
    int ok;
    Schema schema;
    char message[256];
} SchemaResult;

SchemaResult load_schema(const char *schema_dir, const char *data_dir, const char *table_name);
int schema_find_column(const Schema *schema, const char *column_name);
int schema_find_id_column(const Schema *schema);
void free_schema(Schema *schema);

#endif
