// AST는 파싱 결과를 구조체로 담아 두는 단계다.
#ifndef AST_H
#define AST_H

#include "sqlparser/common/util.h"

// 현재 프로젝트는 INSERT와 SELECT 두 종류의 문장만 지원한다.
typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType;

// INSERT 문장을 구조체로 표현한 형태다.
typedef struct {
    // 데이터를 넣을 테이블 이름이다.
    char *table_name;
    // INSERT 문장에 적힌 컬럼 목록이다.
    StringList columns;
    // 각 컬럼에 들어갈 값 목록이다.
    StringList values;
} InsertStatement;

// SELECT 문장을 구조체로 표현한 형태다.
typedef struct {
    // 데이터를 읽을 테이블 이름이다.
    char *table_name;
    // 조회할 컬럼 목록이다. select_all 이 1이면 비워둘 수 있다.
    StringList columns;
    // 1이면 SELECT * 이고, 0이면 특정 컬럼 조회다.
    int select_all;
    // 1이면 WHERE 절이 포함된 SELECT다.
    int has_where;
    // WHERE 절에서 비교할 컬럼명이다.
    char *where_column;
    // WHERE 절에서 비교할 값이다.
    char *where_value;
} SelectStatement;

// 파싱 결과를 하나의 공통 타입으로 다루기 위한 구조체다.
typedef struct {
    // 지금 담긴 문장이 INSERT인지 SELECT인지 알려준다.
    StatementType type;
    // 실제 문장 데이터는 union 안에 한 종류만 들어간다.
    union {
        InsertStatement insert_statement;
        SelectStatement select_statement;
    } as;
} Statement;

// Statement 내부에서 동적으로 잡은 문자열과 리스트 메모리를 해제한다.
void free_statement(Statement *statement);

#endif
