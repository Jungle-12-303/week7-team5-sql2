/*
 * index/table_index.c
 *
 * 이 파일은 "테이블별 인덱스 운영"을 담당한다.
 * B+ 트리 자체는 bptree.c가 구현하고,
 * 여기서는 각 테이블마다:
 * - 인덱스가 이미 메모리에 있는지
 * - 아직 없으면 CSV에서 재구성해야 하는지
 * - 다음 자동 id가 무엇인지
 * 를 관리한다.
 */
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
static int force_next_register_failure = 0;

/* 레지스트리에서 특정 테이블 이름의 인덱스 엔트리를 찾는다. */
static TableIndex *find_table_index(const char *table_name) {
    int index;

    for (index = 0; index < registry.count; index++) {
        if (strcmp(registry.items[index].table_name, table_name) == 0) {
            return &registry.items[index];
        }
    }

    return NULL;
}

/* 테이블 인덱스 엔트리를 찾고, 없으면 새로 생성한다. */
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

/*
 * CSV 한 행을 읽을 때마다 호출되는 재구성 콜백이다.
 *
 * 각 행에서:
 * - id를 읽고
 * - 정수인지 검사하고
 * - B+ 트리에 삽입하고
 * - max_id를 갱신한다.
 */
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

/*
 * 특정 테이블 인덱스가 메모리에 준비돼 있는지 보장한다.
 *
 * 이미 있으면 그대로 사용하고,
 * 아직 없으면 CSV를 스캔해 B+ 트리를 다시 만든다.
 */
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

/* 모든 테이블 인덱스를 메모리에서 제거하고 레지스트리를 초기 상태로 되돌린다. */
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
    force_next_register_failure = 0;
}

/* 특정 테이블 인덱스를 "다시 재구성 필요" 상태로 무효화한다. */
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

/* 특정 테이블 인덱스가 현재 메모리에 로드돼 있는지 확인한다. */
int table_index_is_loaded(const char *table_name) {
    TableIndex *entry = find_table_index(table_name);
    return entry != NULL && entry->loaded;
}

/* 자동 증가 id에 사용할 next_id 값을 구한다. */
int table_index_get_next_id(const Schema *schema, const char *data_dir, int *next_id, char *message, size_t message_size) {
    TableIndex *entry;

    if (!ensure_loaded(schema, data_dir, &entry, message, message_size)) {
        return 0;
    }

    *next_id = entry->next_id;
    return 1;
}

/* CSV에 새 행이 추가된 뒤, 그 id와 오프셋을 메모리 인덱스에 등록한다. */
int table_index_register_row(const Schema *schema, const char *data_dir, int id, long row_offset, char *message, size_t message_size) {
    TableIndex *entry;

    if (force_next_register_failure) {
        force_next_register_failure = 0;
        snprintf(message, message_size, "forced index registration failure");
        return 0;
    }

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

/* id 하나를 받아 인덱스에서 해당 CSV 오프셋을 찾는다. */
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

/* 테스트용: 다음 인덱스 등록 한 번을 강제로 실패시키도록 설정한다. */
void table_index_force_next_register_failure(void) {
    force_next_register_failure = 1;
}
