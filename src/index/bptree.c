/*
 * index/bptree.c
 *
 * 이 파일은 메모리 기반 B+ 트리 자료구조 자체를 구현한다.
 * table_index.c가 "테이블별 인덱스 운영"을 맡는다면,
 * bptree.c는 그 아래에서 "키를 어떻게 저장하고 찾는가"만 책임진다.
 */
#include "sqlparser/index/bptree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BPTREE_MAX_KEYS 3

struct BPlusTreeNode {
    int is_leaf;
    int key_count;
    int keys[BPTREE_MAX_KEYS + 1];
    struct BPlusTreeNode *children[BPTREE_MAX_KEYS + 2];
    long values[BPTREE_MAX_KEYS + 1];
    struct BPlusTreeNode *next;
};

typedef struct {
    int ok;
    int split;
    int promoted_key;
    struct BPlusTreeNode *right_node;
    char message[256];
} InsertState;

static InsertState insert_into_node(struct BPlusTreeNode *node, int key, long value);

/* 리프/내부 노드를 구분해 새 B+ 트리 노드를 만든다. */
static struct BPlusTreeNode *create_node(int is_leaf) {
    struct BPlusTreeNode *node = (struct BPlusTreeNode *)calloc(1, sizeof(struct BPlusTreeNode));
    if (node != NULL) {
        node->is_leaf = is_leaf;
    }
    return node;
}

/* 노드와 그 하위 노드를 재귀적으로 모두 해제한다. */
static void free_node(struct BPlusTreeNode *node) {
    int index;

    if (node == NULL) {
        return;
    }

    if (!node->is_leaf) {
        for (index = 0; index <= node->key_count; index++) {
            free_node(node->children[index]);
        }
    }

    free(node);
}

/* 내부 노드에서 어떤 자식으로 내려가야 하는지 계산한다. */
static int find_child_index(const struct BPlusTreeNode *node, int key) {
    int index = 0;

    while (index < node->key_count && key >= node->keys[index]) {
        index++;
    }

    return index;
}

/* 삽입 실패 결과를 만드는 작은 헬퍼 함수다. */
static InsertState make_error(const char *message) {
    InsertState state = {0};
    state.ok = 0;
    snprintf(state.message, sizeof(state.message), "%s", message);
    return state;
}

/*
 * 리프 노드에 키/값을 삽입한다.
 *
 * 노드가 넘치면:
 * - 오른쪽 노드를 새로 만들고
 * - 절반을 옮기고
 * - 부모에게 올릴 promoted_key를 준비한다.
 */
static InsertState insert_into_leaf(struct BPlusTreeNode *node, int key, long value) {
    InsertState state = {0};
    int insert_index = 0;
    int index;
    struct BPlusTreeNode *right;
    int split_index;

    while (insert_index < node->key_count && node->keys[insert_index] < key) {
        insert_index++;
    }

    if (insert_index < node->key_count && node->keys[insert_index] == key) {
        return make_error("duplicate id key");
    }

    for (index = node->key_count; index > insert_index; index--) {
        node->keys[index] = node->keys[index - 1];
        node->values[index] = node->values[index - 1];
    }

    node->keys[insert_index] = key;
    node->values[insert_index] = value;
    node->key_count++;

    state.ok = 1;
    if (node->key_count <= BPTREE_MAX_KEYS) {
        return state;
    }

    right = create_node(1);
    if (right == NULL) {
        return make_error("out of memory while splitting B+ tree leaf");
    }

    split_index = node->key_count / 2;
    right->key_count = node->key_count - split_index;
    for (index = 0; index < right->key_count; index++) {
        right->keys[index] = node->keys[split_index + index];
        right->values[index] = node->values[split_index + index];
    }

    node->key_count = split_index;
    right->next = node->next;
    node->next = right;

    state.split = 1;
    state.promoted_key = right->keys[0];
    state.right_node = right;
    return state;
}

/*
 * 내부 노드 아래로 재귀 삽입을 수행한다.
 * 자식 노드가 분할되면 그 결과를 현재 노드에 반영한다.
 */
static InsertState insert_into_internal(struct BPlusTreeNode *node, int key, long value) {
    InsertState child_state;
    InsertState state = {0};
    int child_index = find_child_index(node, key);
    int index;
    int mid_index;
    struct BPlusTreeNode *right;

    child_state = insert_into_node(node->children[child_index], key, value);
    if (!child_state.ok) {
        return child_state;
    }

    state.ok = 1;
    if (!child_state.split) {
        return state;
    }

    for (index = node->key_count; index > child_index; index--) {
        node->keys[index] = node->keys[index - 1];
    }
    for (index = node->key_count + 1; index > child_index + 1; index--) {
        node->children[index] = node->children[index - 1];
    }

    node->keys[child_index] = child_state.promoted_key;
    node->children[child_index + 1] = child_state.right_node;
    node->key_count++;

    if (node->key_count <= BPTREE_MAX_KEYS) {
        return state;
    }

    right = create_node(0);
    if (right == NULL) {
        return make_error("out of memory while splitting B+ tree internal node");
    }

    mid_index = node->key_count / 2;
    state.split = 1;
    state.promoted_key = node->keys[mid_index];
    state.right_node = right;

    right->key_count = node->key_count - mid_index - 1;
    for (index = 0; index < right->key_count; index++) {
        right->keys[index] = node->keys[mid_index + 1 + index];
    }
    for (index = 0; index <= right->key_count; index++) {
        right->children[index] = node->children[mid_index + 1 + index];
    }

    node->key_count = mid_index;
    return state;
}

/* 현재 노드가 리프인지 내부인지에 따라 적절한 삽입 함수를 호출한다. */
static InsertState insert_into_node(struct BPlusTreeNode *node, int key, long value) {
    if (node->is_leaf) {
        return insert_into_leaf(node, key, value);
    }

    return insert_into_internal(node, key, value);
}

/* 비어 있는 B+ 트리 구조를 초기화한다. */
void bptree_init(BPlusTree *tree) {
    tree->root = NULL;
}

/* 트리 전체 메모리를 해제하고 루트를 NULL로 되돌린다. */
void bptree_free(BPlusTree *tree) {
    free_node(tree->root);
    tree->root = NULL;
}

/*
 * B+ 트리에 key -> value 쌍을 삽입하는 공개 함수다.
 * 루트가 분할되면 새 루트를 만들어 트리 높이를 한 단계 올린다.
 */
int bptree_insert(BPlusTree *tree, int key, long value, char *message, size_t message_size) {
    InsertState state;
    struct BPlusTreeNode *new_root;

    if (tree->root == NULL) {
        tree->root = create_node(1);
        if (tree->root == NULL) {
            snprintf(message, message_size, "out of memory while creating B+ tree root");
            return 0;
        }

        tree->root->keys[0] = key;
        tree->root->values[0] = value;
        tree->root->key_count = 1;
        return 1;
    }

    state = insert_into_node(tree->root, key, value);
    if (!state.ok) {
        snprintf(message, message_size, "%s", state.message);
        return 0;
    }

    if (!state.split) {
        return 1;
    }

    new_root = create_node(0);
    if (new_root == NULL) {
        snprintf(message, message_size, "out of memory while growing B+ tree root");
        return 0;
    }

    new_root->keys[0] = state.promoted_key;
    new_root->children[0] = tree->root;
    new_root->children[1] = state.right_node;
    new_root->key_count = 1;
    tree->root = new_root;
    return 1;
}

/* key를 검색해 대응하는 value(현재는 CSV 오프셋)를 찾아낸다. */
int bptree_search(const BPlusTree *tree, int key, long *value) {
    struct BPlusTreeNode *node = tree->root;
    int index;

    while (node != NULL && !node->is_leaf) {
        node = node->children[find_child_index(node, key)];
    }

    if (node == NULL) {
        return 0;
    }

    for (index = 0; index < node->key_count; index++) {
        if (node->keys[index] == key) {
            if (value != NULL) {
                *value = node->values[index];
            }
            return 1;
        }
    }

    return 0;
}
