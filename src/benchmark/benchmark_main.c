#include "sqlparser/benchmark/benchmark.h"

#include "sqlparser/common/util.h"
#include "sqlparser/execution/executor.h"
#include "sqlparser/index/table_index.h"
#include "sqlparser/storage/schema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int write_csv_header(const Schema *schema, const char *data_dir, char *message, size_t message_size) {
    char *path;
    FILE *file;
    int index;

    path = build_path(data_dir, schema->storage_name, ".csv");
    if (path == NULL) {
        snprintf(message, message_size, "out of memory while building benchmark CSV path");
        return 0;
    }

    file = fopen(path, "wb");
    free(path);
    if (file == NULL) {
        snprintf(message, message_size, "failed to open benchmark CSV for reset");
        return 0;
    }

    for (index = 0; index < schema->columns.count; index++) {
        if (index > 0) {
            fputc(',', file);
        }
        fputs(schema->columns.items[index], file);
    }
    fputc('\n', file);

    if (fclose(file) != 0) {
        snprintf(message, message_size, "failed to finalize benchmark CSV reset");
        return 0;
    }

    return 1;
}

static char *make_value_text(const char *column_name, int row_number) {
    char buffer[128];

    if (strcmp(column_name, "age") == 0) {
        snprintf(buffer, sizeof(buffer), "%d", 20 + (row_number % 50));
    } else {
        snprintf(buffer, sizeof(buffer), "%s_%d", column_name, row_number);
    }

    return copy_string(buffer);
}

static void free_generated_statement(Statement *statement) {
    free_statement(statement);
    memset(statement, 0, sizeof(*statement));
}

static int build_insert_statement(const Schema *schema, int row_number, Statement *statement, char *message, size_t message_size) {
    int index;

    memset(statement, 0, sizeof(*statement));
    statement->type = STATEMENT_INSERT;
    statement->as.insert_statement.table_name = copy_string(schema->table_name);
    if (statement->as.insert_statement.table_name == NULL) {
        snprintf(message, message_size, "out of memory while building benchmark insert");
        return 0;
    }

    for (index = 0; index < schema->columns.count; index++) {
        char *value_text;

        if (strcmp(schema->columns.items[index], "id") == 0) {
            continue;
        }

        if (!string_list_push(&statement->as.insert_statement.columns, schema->columns.items[index])) {
            free_generated_statement(statement);
            snprintf(message, message_size, "out of memory while building benchmark insert");
            return 0;
        }

        value_text = make_value_text(schema->columns.items[index], row_number);
        if (value_text == NULL || !string_list_push(&statement->as.insert_statement.values, value_text)) {
            free(value_text);
            free_generated_statement(statement);
            snprintf(message, message_size, "out of memory while building benchmark insert");
            return 0;
        }
        free(value_text);
    }

    return 1;
}

static int build_select_statement(const char *table_name, const char *column_name, const char *value, Statement *statement) {
    memset(statement, 0, sizeof(*statement));
    statement->type = STATEMENT_SELECT;
    statement->as.select_statement.table_name = copy_string(table_name);
    statement->as.select_statement.where_column = copy_string(column_name);
    statement->as.select_statement.where_value = copy_string(value);
    statement->as.select_statement.select_all = 1;
    statement->as.select_statement.has_where = 1;

    return statement->as.select_statement.table_name != NULL &&
           statement->as.select_statement.where_column != NULL &&
           statement->as.select_statement.where_value != NULL;
}

static double run_query_benchmark(const Statement *statement, const char *schema_dir, const char *data_dir, int repeat_count) {
    FILE *sink;
    clock_t started;
    int index;

    sink = tmpfile();
    if (sink == NULL) {
        return -1.0;
    }

    started = clock();
    for (index = 0; index < repeat_count; index++) {
        ExecResult result = execute_statement(statement, schema_dir, data_dir, sink);
        if (!result.ok) {
            fclose(sink);
            return -1.0;
        }
        clearerr(sink);
        rewind(sink);
    }

    fclose(sink);
    return ((double)(clock() - started) / (double)CLOCKS_PER_SEC) / (double)repeat_count;
}

int benchmark_main(int argc, char *argv[]) {
    const char *schema_dir;
    const char *data_dir;
    const char *table_name;
    int row_count;
    int query_repeat = 100;
    SchemaResult schema_result;
    Statement statement = {0};
    Statement id_select = {0};
    Statement other_select = {0};
    int index;
    int target_id;
    char id_text[32];
    const char *other_column = NULL;
    char *other_value = NULL;
    double indexed_time;
    double linear_time;

    if (argc != 5 && argc != 6) {
        fprintf(stderr, "Usage: %s <schema_dir> <data_dir> <table_name> <row_count> [query_repeat]\n", argv[0]);
        return 1;
    }

    schema_dir = argv[1];
    data_dir = argv[2];
    table_name = argv[3];
    if (!parse_int_strict(argv[4], &row_count) || row_count <= 0) {
        fprintf(stderr, "error: row_count must be a positive integer\n");
        return 1;
    }
    if (argc == 6 && (!parse_int_strict(argv[5], &query_repeat) || query_repeat <= 0)) {
        fprintf(stderr, "error: query_repeat must be a positive integer\n");
        return 1;
    }

    schema_result = load_schema(schema_dir, data_dir, table_name);
    if (!schema_result.ok) {
        fprintf(stderr, "error: %s\n", schema_result.message);
        return 1;
    }

    for (index = 0; index < schema_result.schema.columns.count; index++) {
        if (strcmp(schema_result.schema.columns.items[index], "id") != 0) {
            other_column = schema_result.schema.columns.items[index];
            break;
        }
    }

    if (other_column == NULL) {
        fprintf(stderr, "error: benchmark table must have at least one non-id column\n");
        free_schema(&schema_result.schema);
        return 1;
    }

    if (!write_csv_header(&schema_result.schema, data_dir, schema_result.message, sizeof(schema_result.message))) {
        fprintf(stderr, "error: %s\n", schema_result.message);
        free_schema(&schema_result.schema);
        return 1;
    }

    table_index_registry_reset();
    for (index = 0; index < row_count; index++) {
        ExecResult result;
        if (!build_insert_statement(&schema_result.schema, index + 1, &statement, schema_result.message, sizeof(schema_result.message))) {
            fprintf(stderr, "error: %s\n", schema_result.message);
            free_schema(&schema_result.schema);
            return 1;
        }

        result = execute_statement(&statement, schema_dir, data_dir, stdout);
        free_generated_statement(&statement);
        if (!result.ok) {
            fprintf(stderr, "error: %s\n", result.message);
            free_schema(&schema_result.schema);
            return 1;
        }
    }

    target_id = row_count / 2;
    if (target_id < 1) {
        target_id = 1;
    }

    snprintf(id_text, sizeof(id_text), "%d", target_id);
    other_value = make_value_text(other_column, target_id);
    if (other_value == NULL ||
        !build_select_statement(schema_result.schema.table_name, "id", id_text, &id_select) ||
        !build_select_statement(schema_result.schema.table_name, other_column, other_value, &other_select)) {
        fprintf(stderr, "error: out of memory while preparing benchmark queries\n");
        free(other_value);
        free_generated_statement(&id_select);
        free_generated_statement(&other_select);
        free_schema(&schema_result.schema);
        return 1;
    }

    indexed_time = run_query_benchmark(&id_select, schema_dir, data_dir, query_repeat);
    linear_time = run_query_benchmark(&other_select, schema_dir, data_dir, query_repeat);
    if (indexed_time < 0.0 || linear_time < 0.0) {
        fprintf(stderr, "error: failed to execute benchmark query\n");
        free(other_value);
        free_generated_statement(&id_select);
        free_generated_statement(&other_select);
        free_schema(&schema_result.schema);
        return 1;
    }

    printf("Inserted rows: %d\n", row_count);
    printf("Query repeats: %d\n", query_repeat);
    printf("Indexed query avg time: %.6f sec\n", indexed_time);
    printf("Linear query avg time: %.6f sec\n", linear_time);

    free(other_value);
    free_generated_statement(&id_select);
    free_generated_statement(&other_select);
    free_schema(&schema_result.schema);
    table_index_registry_reset();
    return 0;
}

#ifndef SQLPARSER_BENCHMARK_NO_MAIN
int main(int argc, char *argv[]) {
    return benchmark_main(argc, argv);
}
#endif
