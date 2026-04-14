#ifndef SQLPARSER_INDEX_BPTREE_H
#define SQLPARSER_INDEX_BPTREE_H

#include <stddef.h>

typedef struct BPlusTreeNode BPlusTreeNode;

typedef struct {
    BPlusTreeNode *root;
} BPlusTree;

void bptree_init(BPlusTree *tree);
void bptree_free(BPlusTree *tree);
int bptree_insert(BPlusTree *tree, int key, long value, char *message, size_t message_size);
int bptree_search(const BPlusTree *tree, int key, long *value);

#endif
