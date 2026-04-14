# SqlParser 초보자 가이드

이 문서는 현재 폴더의 코드를 기준으로, 비전공자도 프로젝트 구조를 따라갈 수 있게 설명하는 입문 문서입니다.

## 1. 이 프로젝트가 하는 일

이 프로젝트는 아주 작은 파일 기반 SQL 처리기입니다.

사용자가 `INSERT` 나 `SELECT` 문장을 입력하면 프로그램은 아래 순서로 동작합니다.

1. SQL 문장을 읽습니다.
2. 문장을 잘게 나눕니다.
3. 문장이 무슨 뜻인지 해석합니다.
4. 실제 `schema/` 와 `data/` 폴더를 읽거나 수정합니다.
5. 결과를 화면에 출력합니다.

즉, "SQL 문장 -> 해석 -> 파일 읽기/쓰기 -> 결과 출력" 흐름으로 생각하면 됩니다.

## 2. 가장 먼저 이해하면 좋은 흐름

이 프로젝트를 처음 볼 때는 아래 순서로 읽는 것이 가장 쉽습니다.

1. [src/app/main.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/app/main.c)
2. [src/sql/lexer.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/sql/lexer.c)
3. [src/sql/parser.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/sql/parser.c)
4. [src/execution/executor.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/execution/executor.c)
5. [src/storage/schema.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/storage/schema.c)
6. [src/storage/storage.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/storage/storage.c)

이 순서대로 보면 "입력 -> 해석 -> 실행 -> 저장" 흐름이 자연스럽게 이어집니다.

## 3. 폴더 구조 한눈에 보기

- [src/app](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/app)
  프로그램 시작점이 있습니다.
- [src/sql](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/sql)
  SQL 문장을 읽고 해석하는 코드가 있습니다.
- [src/execution](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/execution)
  해석된 SQL을 실제 동작으로 바꾸는 코드가 있습니다.
- [src/storage](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/storage)
  스키마와 CSV 파일을 읽고 쓰는 코드가 있습니다.
- [src/common](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/common)
  문자열, 파일, 리스트 같은 공용 도구 함수가 있습니다.
- [include/sqlparser](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/include/sqlparser)
  `.h` 헤더 파일이 모여 있습니다.
- [schema](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/schema)
  테이블 구조 정보가 있습니다.
- [data](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/data)
  실제 데이터가 CSV 형태로 저장됩니다.
- [examples](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/examples)
  예제 SQL 파일이 있습니다.
- [tests](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/tests)
  테스트 코드가 있습니다.

## 4. 핵심 용어를 아주 쉽게 설명하면

- `lexer`
  긴 SQL 문장을 작은 조각으로 자르는 단계입니다.
  예: `SELECT`, `name`, `FROM`, `users`, `;`
- `parser`
  잘린 조각들을 보고 문장의 의미를 해석하는 단계입니다.
- `AST`
  해석 결과를 담아두는 구조체입니다.
  사람이 읽는 SQL을 컴퓨터가 다루기 쉬운 모양으로 바꾼 중간 결과라고 생각하면 됩니다.
- `schema`
  테이블 이름과 컬럼 순서 같은 "구조 정보"입니다.
- `executor`
  해석된 결과를 실제 파일 읽기/쓰기 동작으로 바꾸는 부분입니다.

## 5. 실제 실행 흐름

### 5-1. 시작점

[src/app/main.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/app/main.c) 가 프로그램의 시작점입니다.

이 파일은 아래 일을 합니다.

1. 사용자가 SQL 파일을 줬는지, SQL 문장을 직접 줬는지 확인
2. SQL 문자열 준비
3. `lex_sql()` 호출
4. `parse_statement()` 호출
5. `execute_statement()` 호출
6. 결과 메시지 출력

즉 `main.c` 는 전체 흐름을 연결하는 지휘자 역할입니다.

### 5-2. SQL을 조각내기

[src/sql/lexer.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/sql/lexer.c) 는 SQL 문장을 토큰으로 나눕니다.

예를 들어 아래 SQL이 있으면:

```sql
SELECT name, age FROM users;
```

대략 이런 조각으로 나눕니다.

- `SELECT`
- `name`
- `,`
- `age`
- `FROM`
- `users`
- `;`

사람이 띄어쓰기와 기호를 보고 구분하듯, 프로그램도 먼저 잘게 나눠야 다음 단계로 갈 수 있습니다.

### 5-3. SQL을 이해하기

[src/sql/parser.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/sql/parser.c) 는 토큰 목록을 보고 문장을 해석합니다.

예를 들어:

```sql
INSERT INTO users (id, name, age) VALUES (1, 'kim', 20);
```

를 해석한 결과는 대략 이런 정보가 됩니다.

- 문장 종류: `INSERT`
- 테이블 이름: `users`
- 컬럼 목록: `id`, `name`, `age`
- 값 목록: `1`, `kim`, `20`

이 정보를 담는 구조체 정의가 [include/sqlparser/sql/ast.h](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/include/sqlparser/sql/ast.h) 에 있습니다.

### 5-4. 실제 동작 수행하기

[src/execution/executor.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/execution/executor.c) 는 parser가 만든 결과를 실제 동작으로 바꿉니다.

- `INSERT` 면 CSV 파일에 한 줄을 추가합니다.
- `SELECT` 면 CSV 파일을 읽어서 필요한 컬럼만 출력합니다.

즉 executor는 "해석된 뜻"을 "실제 행동"으로 바꾸는 단계입니다.

### 5-5. 스키마와 CSV 다루기

[src/storage/schema.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/storage/schema.c) 는 테이블 구조가 맞는지 확인합니다.

예를 들어 `users` 테이블이라면 아래 두 파일을 함께 봅니다.

- [schema/users.meta](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/schema/users.meta)
- [data/users.csv](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/data/users.csv)

여기서 확인하는 것은 주로 아래 내용입니다.

- 메타 파일이 존재하는가
- 테이블 이름이 맞는가
- 컬럼 목록이 있는가
- CSV 헤더와 스키마 컬럼 순서가 같은가

[src/storage/storage.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/storage/storage.c) 는 실제 CSV 한 줄을 읽고, 나누고, 추가하는 일을 맡습니다.

## 6. 데이터 파일은 어떻게 생겼나

예를 들어 `users` 테이블은 메타 파일이 이런 식입니다.

```txt
table=users
columns=id,name,age
```

CSV 파일은 보통 이렇게 시작합니다.

```txt
id,name,age
1,kim,20
2,lee,25
```

즉:

- `schema/*.meta` = 구조 설명서
- `data/*.csv` = 실제 데이터

라고 생각하면 이해하기 쉽습니다.

## 7. 현재 지원하는 SQL

지금 이 프로젝트는 아주 작은 범위의 SQL만 지원합니다.

- `INSERT INTO table_name (col1, col2, ...) VALUES (...);`
- `SELECT * FROM table_name;`
- `SELECT col1, col2 FROM table_name;`

아직 지원하지 않는 기능 예시:

- `WHERE`
- `JOIN`
- `UPDATE`
- `DELETE`
- `CREATE TABLE`

## 8. 직접 실행해보기

Dev Container bash 기준으로는 아래처럼 빌드하면 됩니다.

```bash
cd /workspaces/sqlparser/.codex-main-pr
mkdir -p build/bin

gcc -Wall -Wextra -std=c11 -Iinclude \
  -o build/bin/sqlparser \
  src/app/main.c \
  src/common/util.c \
  src/storage/schema.c \
  src/storage/storage.c \
  src/sql/ast.c \
  src/sql/lexer.c \
  src/sql/parser.c \
  src/execution/executor.c
```

실행 예시:

```bash
./build/bin/sqlparser ./examples/select_all_users.sql
./build/bin/sqlparser "SELECT * FROM users;"
./build/bin/sqlparser "SELECT name, age FROM users;"
./build/bin/sqlparser "INSERT INTO users (id, name, age) VALUES (1, 'kim', 20);"
```

주의:

- `SELECT * FROM users;` 는 `*` 때문에 큰따옴표로 감싸는 것이 안전합니다.
- `INSERT` 를 실행하면 실제 CSV 파일 내용이 바뀝니다.

## 9. 초보자 기준으로 꼭 기억하면 좋은 점

- 이 프로젝트는 진짜 데이터베이스 서버가 아닙니다.
- 데이터를 메모리가 아니라 파일에 저장합니다.
- SQL을 바로 실행하는 것이 아니라 `읽기 -> 해석 -> 실행` 단계를 거칩니다.
- `AST` 는 중간 해석 결과를 담는 상자입니다.
- `schema` 는 테이블 구조 설명서입니다.
- `executor` 는 해석된 내용을 실제 파일 작업으로 바꾸는 부분입니다.

## 10. 디버깅할 때 어디에 브레이크포인트를 두면 좋은가

처음 디버깅할 때는 아래 위치가 이해하기 좋습니다.

- [src/app/main.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/app/main.c)
  `load_sql_input`, `lex_sql`, `parse_statement`, `execute_statement` 호출 부분
- [src/sql/parser.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/sql/parser.c)
  `INSERT` 와 `SELECT` 를 구분하는 부분
- [src/execution/executor.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/execution/executor.c)
  `execute_insert`, `execute_select`
- [src/storage/schema.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/storage/schema.c)
  `load_schema`
- [src/storage/storage.c](/C:/Users/home/workspace/jungle/wedcoding/week6/sqlparser/.codex-main-pr/src/storage/storage.c)
  CSV 읽기/쓰기 함수

## 11. 마지막으로 한 문장 정리

이 프로젝트는 "간단한 SQL 문장을 읽어서 CSV 파일을 작은 데이터베이스처럼 다루는 프로그램"입니다.

처음에는 어렵게 보여도 아래 한 줄만 계속 기억하면 됩니다.

`main -> lexer -> parser -> executor -> schema/storage`
