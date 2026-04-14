#ifndef STORAGE_H
#define STORAGE_H

#include "sqlparser/common/util.h"

#include <stddef.h>

typedef struct {
    int ok;
    int affected_rows;
    long row_offset;
    char message[256];
} StorageResult;

typedef struct {
    int ok;
    StringList fields;
    char message[256];
} StorageReadResult;

typedef int (*StorageRowVisitor)(const StringList *fields, long row_offset, void *context, char *error, size_t error_size);

int csv_parse_line(const char *line, StringList *fields, char *error, size_t error_size);
char *csv_escape_field(const char *value);
StorageResult append_row_csv(const char *data_dir, const char *table_name, const StringList *row_values);
StorageReadResult read_row_at_offset_csv(const char *data_dir, const char *table_name, long row_offset);
int scan_rows_csv(const char *data_dir, const char *table_name, StorageRowVisitor visitor, void *context, char *error, size_t error_size);

#endif
