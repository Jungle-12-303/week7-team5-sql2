/*
 * execution/executor.c
 *
 * executor는 parser가 만든 AST를 실제 동작으로 연결하는 계층이다.
 * 초심자 관점에서는 "SQL 문장의 의미를 실제 파일 작업으로 번역하는 오케스트레이터"다.
 *
 * 이 파일의 핵심 책임:
 * - INSERT / SELECT 분기
 * - INSERT 시 자동 id 생성 흐름 조정
 * - WHERE id일 때 인덱스 경로 선택
 * - 일반 SELECT / 일반 WHERE일 때 기존 CSV 스캔 경로 유지
 */
#include "sqlparser/execution/executor.h"

#include "sqlparser/common/util.h"
#include "sqlparser/index/table_index.h"
#include "sqlparser/storage/schema.h"
#include "sqlparser/storage/storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 실행 실패 결과를 공통 형식으로 채우는 작은 헬퍼 함수다. */
static void set_exec_error(ExecResult *result, const char *message) {
    result->ok = 0;
    snprintf(result->message, sizeof(result->message), "%s", message);
}

/* CSV 한 칸 값에 줄바꿈이 들어 있는지 검사한다. */
static int contains_newline(const char *value) {
    return strchr(value, '\n') != NULL || strchr(value, '\r') != NULL;
}

/* 자동 생성한 정수 id를 문자열로 바꿔 INSERT 행에 넣기 위한 함수다. */
static char *copy_int_string(int value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return copy_string(buffer);
}

/* INSERT에 들어온 컬럼 목록과 값 목록이 스키마 기준으로 유효한지 검사한다. */
static int validate_insert_columns(const InsertStatement *statement, const Schema *schema, char *message, size_t message_size) {
    int index;

    if (statement->columns.count != statement->values.count) {
        snprintf(message, message_size, "column count and value count do not match");
        return 0;
    }

    for (index = 0; index < statement->columns.count; index++) {
        if (schema_find_column(schema, statement->columns.items[index]) < 0) {
            snprintf(message, message_size, "unknown column in INSERT: %s", statement->columns.items[index]);
            return 0;
        }

        if (contains_newline(statement->values.items[index])) {
            snprintf(message, message_size, "newline in value is not supported");
            return 0;
        }
    }

    return 1;
}

/*
 * 사용자가 준 INSERT 입력을 스키마 순서에 맞는 "완성 행"으로 바꾼다.
 *
 * 예:
 * - 사용자는 name만 넣을 수 있다.
 * - 누락 컬럼은 빈 문자열로 채운다.
 * - id는 시스템이 계산한 next_id로 덮어쓴다.
 */
static int build_insert_row(const InsertStatement *statement, const Schema *schema, int next_id, StringList *row_values, char *message, size_t message_size) {
    int *assigned = NULL;
    int schema_index;
    int value_index;
    int id_index;

    id_index = schema_find_id_column(schema);
    if (id_index < 0) {
        snprintf(message, message_size, "id column is required for INSERT");
        return 0;
    }

    assigned = (int *)calloc((size_t)schema->columns.count, sizeof(int));
    if (assigned == NULL) {
        snprintf(message, message_size, "out of memory while preparing INSERT row");
        return 0;
    }

    for (schema_index = 0; schema_index < schema->columns.count; schema_index++) {
        if (!string_list_push(row_values, "")) {
            free(assigned);
            string_list_free(row_values);
            snprintf(message, message_size, "out of memory while preparing INSERT row");
            return 0;
        }
    }

    for (value_index = 0; value_index < statement->columns.count; value_index++) {
        schema_index = schema_find_column(schema, statement->columns.items[value_index]);
        if (schema_index < 0) {
            free(assigned);
            string_list_free(row_values);
            snprintf(message, message_size, "unknown column in INSERT: %s", statement->columns.items[value_index]);
            return 0;
        }

        if (assigned[schema_index]) {
            free(assigned);
            string_list_free(row_values);
            snprintf(message, message_size, "duplicate column in INSERT: %s", statement->columns.items[value_index]);
            return 0;
        }

        assigned[schema_index] = 1;
        free(row_values->items[schema_index]);
        row_values->items[schema_index] = copy_string(statement->values.items[value_index]);
        if (row_values->items[schema_index] == NULL) {
            free(assigned);
            string_list_free(row_values);
            snprintf(message, message_size, "out of memory while preparing INSERT row");
            return 0;
        }
    }

    free(row_values->items[id_index]);
    row_values->items[id_index] = copy_int_string(next_id);
    free(assigned);

    if (row_values->items[id_index] == NULL) {
        string_list_free(row_values);
        snprintf(message, message_size, "out of memory while preparing INSERT row");
        return 0;
    }

    return 1;
}

/*
 * INSERT 실행의 전체 흐름을 담당한다.
 *
 * 순서:
 * 1. schema 로딩
 * 2. 컬럼/값 검증
 * 3. next_id 계산
 * 4. CSV에 쓸 행 구성
 * 5. CSV append
 * 6. 인덱스 등록
 */
static ExecResult execute_insert(const InsertStatement *statement, const char *schema_dir, const char *data_dir) {
    ExecResult result = {0};
    SchemaResult schema_result;
    StringList row_values = {0};
    StorageResult storage_result;
    int next_id;

    schema_result = load_schema(schema_dir, data_dir, statement->table_name);
    if (!schema_result.ok) {
        set_exec_error(&result, schema_result.message);
        return result;
    }

    if (!validate_insert_columns(statement, &schema_result.schema, result.message, sizeof(result.message))) {
        free_schema(&schema_result.schema);
        return result;
    }

    if (!table_index_get_next_id(&schema_result.schema, data_dir, &next_id, result.message, sizeof(result.message))) {
        free_schema(&schema_result.schema);
        return result;
    }

    if (!build_insert_row(statement, &schema_result.schema, next_id, &row_values, result.message, sizeof(result.message))) {
        free_schema(&schema_result.schema);
        return result;
    }

    storage_result = append_row_csv(data_dir, schema_result.schema.storage_name, &row_values);
    string_list_free(&row_values);
    if (!storage_result.ok) {
        free_schema(&schema_result.schema);
        set_exec_error(&result, storage_result.message);
        return result;
    }

    if (!table_index_register_row(&schema_result.schema, data_dir, next_id, storage_result.row_offset, result.message, sizeof(result.message))) {
        table_index_invalidate(schema_result.schema.storage_name);
        free_schema(&schema_result.schema);
        return result;
    }

    free_schema(&schema_result.schema);
    result.ok = 1;
    result.affected_rows = storage_result.affected_rows;
    snprintf(result.message, sizeof(result.message), "%s", storage_result.message);
    return result;
}

/* SELECT 결과에서 어떤 컬럼을 어떤 순서로 출력할지 계산한다. */
static int build_select_indexes(const SelectStatement *statement, const Schema *schema, StringList *selected_headers, int *selected_indexes, char *message, size_t message_size) {
    int index;
    int schema_index;

    if (statement->select_all) {
        for (index = 0; index < schema->columns.count; index++) {
            if (!string_list_push(selected_headers, schema->columns.items[index])) {
                snprintf(message, message_size, "out of memory while preparing SELECT");
                return 0;
            }
            selected_indexes[index] = index;
        }
        return schema->columns.count;
    }

    for (index = 0; index < statement->columns.count; index++) {
        schema_index = schema_find_column(schema, statement->columns.items[index]);
        if (schema_index < 0) {
            snprintf(message, message_size, "unknown column in SELECT: %s", statement->columns.items[index]);
            return 0;
        }

        if (!string_list_push(selected_headers, statement->columns.items[index])) {
            snprintf(message, message_size, "out of memory while preparing SELECT");
            return 0;
        }

        selected_indexes[index] = schema_index;
    }

    return statement->columns.count;
}

/* WHERE 절에 사용된 컬럼이 스키마에서 몇 번째인지 찾는다. */
static int resolve_where_index(const SelectStatement *statement, const Schema *schema, int *where_index, char *message, size_t message_size) {
    if (!statement->has_where) {
        *where_index = -1;
        return 1;
    }

    *where_index = schema_find_column(schema, statement->where_column);
    if (*where_index < 0) {
        snprintf(message, message_size, "unknown column in WHERE: %s", statement->where_column);
        return 0;
    }

    return 1;
}

/* 현재 CSV 행이 WHERE 조건과 일치하는지 검사한다. */
static int row_matches_where(const SelectStatement *statement, const StringList *fields, int where_index) {
    if (!statement->has_where) {
        return 1;
    }

    return strcmp(fields->items[where_index], statement->where_value) == 0;
}

/* 선택된 컬럼만 골라 한 줄 결과로 출력한다. */
static void print_selected_row(FILE *out, const StringList *fields, const int *selected_indexes, int selected_count) {
    int index;

    for (index = 0; index < selected_count; index++) {
        if (index > 0) {
            fprintf(out, " | ");
        }
        fprintf(out, "%s", fields->items[selected_indexes[index]]);
    }
    fprintf(out, "\n");
}

/* 결과 표의 헤더 행을 출력한다. */
static void print_header_row(FILE *out, const StringList *headers) {
    int index;

    for (index = 0; index < headers->count; index++) {
        if (index > 0) {
            fprintf(out, " | ");
        }
        fprintf(out, "%s", headers->items[index]);
    }
    fprintf(out, "\n");
}

/*
 * WHERE id = ... 전용 인덱스 조회 경로를 수행한다.
 *
 * 일반 SELECT와 달리:
 * - id 값을 정수로 검증하고
 * - B+ 트리에서 오프셋을 찾고
 * - 해당 행 하나만 읽는다.
 */
static int execute_index_select(const SelectStatement *statement, const Schema *schema, const char *data_dir, FILE *out, const StringList *headers, const int *selected_indexes, int selected_count, ExecResult *result) {
    TableIndexLookupResult lookup;
    StorageReadResult read_result;
    int where_id;

    if (!parse_int_strict(statement->where_value, &where_id)) {
        set_exec_error(result, "WHERE id value must be an integer");
        return 0;
    }

    lookup = table_index_find_row(schema, data_dir, where_id);
    if (!lookup.ok) {
        set_exec_error(result, lookup.message);
        return 0;
    }

    if (!lookup.found) {
        print_header_row(out, headers);
        result->ok = 1;
        result->affected_rows = 0;
        snprintf(result->message, sizeof(result->message), "SELECT 0");
        return 1;
    }

    read_result = read_row_at_offset_csv(data_dir, schema->storage_name, lookup.row_offset);
    if (!read_result.ok) {
        set_exec_error(result, read_result.message);
        return 0;
    }

    if (read_result.fields.count != schema->columns.count) {
        string_list_free(&read_result.fields);
        set_exec_error(result, "CSV row does not match schema column count");
        return 0;
    }

    print_header_row(out, headers);
    print_selected_row(out, &read_result.fields, selected_indexes, selected_count);
    string_list_free(&read_result.fields);
    result->ok = 1;
    result->affected_rows = 1;
    snprintf(result->message, sizeof(result->message), "SELECT 1");
    return 1;
}

/*
 * SELECT 실행의 전체 흐름을 담당한다.
 *
 * 여기서 가장 중요한 분기:
 * - WHERE id 이면 execute_index_select
 * - 아니면 CSV 전체 스캔
 */
static ExecResult execute_select(const SelectStatement *statement, const char *schema_dir, const char *data_dir, FILE *out) {
    ExecResult result = {0};
    SchemaResult schema_result;
    char *path;
    FILE *file;
    char line[4096];
    StringList fields = {0};
    StringList headers = {0};
    int *selected_indexes = NULL;
    int selected_count;
    int where_index = -1;
    int row_count = 0;

    schema_result = load_schema(schema_dir, data_dir, statement->table_name);
    if (!schema_result.ok) {
        set_exec_error(&result, schema_result.message);
        return result;
    }

    selected_indexes = (int *)calloc((size_t)schema_result.schema.columns.count, sizeof(int));
    if (selected_indexes == NULL) {
        free_schema(&schema_result.schema);
        set_exec_error(&result, "out of memory while preparing SELECT");
        return result;
    }

    selected_count = build_select_indexes(statement, &schema_result.schema, &headers, selected_indexes, result.message, sizeof(result.message));
    if (selected_count == 0) {
        free(selected_indexes);
        string_list_free(&headers);
        free_schema(&schema_result.schema);
        return result;
    }

    if (!resolve_where_index(statement, &schema_result.schema, &where_index, result.message, sizeof(result.message))) {
        free(selected_indexes);
        string_list_free(&headers);
        free_schema(&schema_result.schema);
        return result;
    }

    if (statement->has_where && strcmp(statement->where_column, "id") == 0) {
        execute_index_select(statement, &schema_result.schema, data_dir, out, &headers, selected_indexes, selected_count, &result);
        free(selected_indexes);
        string_list_free(&headers);
        free_schema(&schema_result.schema);
        return result;
    }

    print_header_row(out, &headers);

    path = build_path(data_dir, schema_result.schema.storage_name, ".csv");
    if (path == NULL) {
        free(selected_indexes);
        string_list_free(&headers);
        free_schema(&schema_result.schema);
        set_exec_error(&result, "out of memory while opening table data");
        return result;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        free(selected_indexes);
        string_list_free(&headers);
        free_schema(&schema_result.schema);
        format_system_error(result.message, sizeof(result.message), "failed to open table data file", path);
        free(path);
        result.ok = 0;
        return result;
    }
    free(path);

    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        free(selected_indexes);
        string_list_free(&headers);
        free_schema(&schema_result.schema);
        set_exec_error(&result, "table data file is empty");
        return result;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        strip_line_endings(line);
        if (line[0] == '\0') {
            continue;
        }

        if (!csv_parse_line(line, &fields, result.message, sizeof(result.message))) {
            fclose(file);
            free(selected_indexes);
            string_list_free(&headers);
            string_list_free(&fields);
            free_schema(&schema_result.schema);
            result.ok = 0;
            return result;
        }

        if (fields.count != schema_result.schema.columns.count) {
            fclose(file);
            free(selected_indexes);
            string_list_free(&headers);
            string_list_free(&fields);
            free_schema(&schema_result.schema);
            set_exec_error(&result, "CSV row does not match schema column count");
            return result;
        }

        if (!row_matches_where(statement, &fields, where_index)) {
            string_list_free(&fields);
            continue;
        }

        print_selected_row(out, &fields, selected_indexes, selected_count);
        string_list_free(&fields);
        row_count++;
    }

    fclose(file);
    free(selected_indexes);
    string_list_free(&headers);
    free_schema(&schema_result.schema);

    result.ok = 1;
    result.affected_rows = row_count;
    snprintf(result.message, sizeof(result.message), "SELECT %d", row_count);
    return result;
}

/* Statement 종류에 따라 INSERT 또는 SELECT 실행 함수로 분기하는 진입점이다. */
ExecResult execute_statement(const Statement *statement, const char *schema_dir, const char *data_dir, FILE *out) {
    if (statement->type == STATEMENT_INSERT) {
        return execute_insert(&statement->as.insert_statement, schema_dir, data_dir);
    }

    return execute_select(&statement->as.select_statement, schema_dir, data_dir, out);
}

/* 실행 계층이 내부적으로 쓰는 런타임 상태(현재는 인덱스 레지스트리)를 정리한다. */
void execution_runtime_reset(void) {
    table_index_registry_reset();
}
