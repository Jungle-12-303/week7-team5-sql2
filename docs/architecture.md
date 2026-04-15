# SQL Processor 7주차 아키텍처 문서

## 1. 문서 목적

이 문서는 루트 `SqlParser` 프로젝트에 7주차 요구사항인 `ID 자동 부여`와 `B+ 트리 인덱스`를 추가할 때의 구조와 책임 분리를 정의한다.

목표는 현재 코드의 흐름을 최대한 유지하면서, 인덱스 기능을 별도 모듈로 추가해 확장하는 것이다.

## 2. 설계 원칙

아키텍처는 아래 원칙을 따른다.

- 기존 `app -> sql -> execution -> storage` 흐름을 유지한다.
- SQL 문법 해석과 인덱스 사용 결정은 분리한다.
- 영속 저장의 기준은 계속 CSV 파일이다.
- B+ 트리는 메모리 기반 보조 인덱스다.
- 인덱스 모듈은 SQL 문법을 해석하지 않는다.
- 성능 테스트 코드는 일반 실행 경로와 분리한다.

## 3. 현재 기준 구조

현재 루트 프로젝트의 주요 계층은 아래와 같다.

- `src/app`
  입력 방식 결정, REPL, CLI 흐름 시작
- `src/sql`
  lexer, parser, AST
- `src/execution`
  파싱 결과를 실제 동작으로 연결
- `src/storage`
  schema 검증, CSV 읽기/쓰기
- `src/common`
  공용 문자열/파일 유틸

7주차에서는 여기에 `index` 계층을 추가한다.

## 4. 목표 구조

7주차 기준 권장 구조는 아래와 같다.

```text
src/
  app/
    main.c
  common/
    util.c
  sql/
    lexer.c
    parser.c
    ast.c
  execution/
    executor.c
  storage/
    schema.c
    storage.c
  index/
    bptree.c
    table_index.c
  benchmark/
    benchmark_main.c

include/sqlparser/
  common/
  sql/
  execution/
  storage/
  index/
  benchmark/
```

## 5. 계층별 책임

### 5.1 app 계층

책임:

- 명령행 인자 처리
- SQL 파일 입력 / 직접 입력 / REPL 처리
- 일반 SQL 처리기의 실행 흐름 시작

비책임:

- SQL 문법 해석
- B+ 트리 직접 조작
- CSV 직접 읽기/쓰기

### 5.2 sql 계층

책임:

- SQL 문자열을 토큰으로 분리
- `INSERT`, `SELECT`, `SELECT ... WHERE` 파싱
- `WHERE id = value`를 AST로 표현

비책임:

- 인덱스 사용 여부 판단
- 레코드 저장
- `id` 자동 생성

### 5.3 execution 계층

책임:

- 현재 문장이 `INSERT`인지 `SELECT`인지 분기
- `INSERT`일 때 ID 자동 생성 흐름 orchestrate
- `SELECT ... WHERE id = value`이면 `value`를 정수로 검증한 뒤 인덱스 경로 선택
- 일반 `SELECT` 및 일반 `WHERE`는 기존 storage 경로 사용

비책임:

- B+ 트리 내부 분할 로직
- CSV 파싱 상세 구현

즉, execution 계층은 7주차에서 가장 중요한 오케스트레이션 계층이다.

### 5.4 storage 계층

책임:

- 스키마 파일 로딩
- CSV 헤더 검증
- 행 append
- 전체 스캔 기반 SELECT
- 특정 파일 위치의 행 읽기
- 인덱스 재구성용 CSV 순회
- 인덱스 재구성 시 `id` 컬럼 값 검증에 필요한 원본 행 정보 제공

비책임:

- B+ 트리 탐색 규칙
- SQL 문법 해석

### 5.5 index 계층

책임:

- B+ 트리 생성
- 키 삽입
- 키 탐색
- 테이블별 자동 인덱스 객체 유지
- 인덱스 재구성 상태 관리
- 중복 키 거부

비책임:

- SQL 파싱
- 스키마 파일 해석
- 사용자 출력 포맷 생성

## 6. 핵심 데이터 흐름

### 6.1 INSERT 흐름

7주차 `INSERT`의 목표 흐름은 아래와 같다.

1. `app/main.c`가 SQL 입력을 읽는다.
2. `sql/parser.c`가 `InsertStatement`를 만든다.
3. `execution/executor.c`가 대상 테이블 스키마를 확인한다.
4. execution 계층이 현재 테이블의 최대 `id`를 찾거나 다음 `id`를 계산한다.
5. execution 계층이 완성된 행 데이터를 storage 계층에 전달한다.
6. `storage/storage.c`가 CSV 끝에 새 행을 append한다.
7. append 성공 후 execution 계층이 새 행의 위치 정보를 받는다.
8. `index/table_index.c`가 `id -> 위치 정보`를 B+ 트리에 등록한다.

만약 8단계의 인덱스 등록이 실패하면 현재 실행은 오류를 반환해야 한다. 이때 영속 저장의 기준은 CSV이므로, 해당 테이블의 메모리 인덱스는 무효화하고 다음 인덱스 접근 시 CSV를 기준으로 재구성해 일관성을 회복한다.

핵심은 `CSV 저장 성공 후 인덱스 등록` 순서를 지키는 것이다.

### 6.2 SELECT 흐름

#### 일반 SELECT

1. parser가 `SelectStatement`를 만든다.
2. executor가 `WHERE` 유무와 컬럼 정보를 확인한다.
3. `WHERE`가 없거나 `id`가 아니면 기존 storage 전체 스캔 경로를 사용한다.

#### 인덱스 SELECT

1. parser가 `WHERE id = value`를 포함한 `SelectStatement`를 만든다.
2. executor가 조건 컬럼이 `id`인지, 비교값이 정수인지 확인한다.
3. 해당 테이블 인덱스가 없거나 무효화된 상태면 storage를 이용해 인덱스를 재구성한다.
4. index 계층이 B+ 트리에서 `id`를 탐색한다.
5. 탐색 성공 시 storage 계층이 해당 위치의 행만 읽는다.
6. executor가 기존 SELECT 출력 형식에 맞춰 결과를 출력한다.

## 7. 인덱스 데이터 구조

현재 권장 구조는 아래와 같다.

### 7.1 키

- `int id`

### 7.2 값

- CSV 파일 내 해당 행의 시작 바이트 오프셋

권장 구현은 파일 기반 저장소에서 흔히 쓰는 방식인 `바이트 오프셋` 참조다.

오프셋 기반을 권장하는 이유:

- 현재 저장소가 CSV append 구조다.
- `UPDATE`, `DELETE`가 없어서 기존 행 위치가 비교적 안정적이다.
- 특정 행만 직접 읽기 쉬워진다.
- 인덱스 조회와 선형 탐색의 차이를 명확히 보여 주기 좋다.

### 7.3 테이블 인덱스 관리자

`id` 컬럼이 있는 모든 테이블을 대상으로 테이블별 인덱스 상태를 자동 관리하는 래퍼가 필요하다.

예시 개념:

```c
typedef struct {
    char *table_name;
    int loaded;
    BPlusTree tree;
    int next_id;
} TableIndex;
```

역할:

- 특정 테이블 인덱스가 이미 메모리에 있는지 확인
- 아직 없으면 CSV로부터 재구성
- 인덱스 등록 실패 등으로 무효화된 인덱스를 다시 재구성
- 다음 자동 증가 `id` 추적

## 8. 인덱스 재구성 전략

인덱스는 메모리 기반이므로 프로세스 시작 시 비어 있다.

따라서 아래 전략을 사용한다.

- 테이블 첫 접근 시 lazy load
- storage 계층이 CSV를 처음부터 끝까지 읽는다
- 각 행의 `id`와 시작 오프셋을 추출한다
- index 계층이 B+ 트리에 순서대로 삽입한다
- 동시에 가장 큰 `id`를 기록해 `next_id` 계산에 사용한다
- 재구성 중 정수가 아닌 `id`, 누락된 `id`, 중복 `id`를 만나면 재구성을 실패시키고 오류를 반환한다

이 방식은 현재 파일 기반 구조와 가장 잘 맞는다.

## 9. 모듈 간 의존성

의존성 방향은 아래를 유지한다.

```text
app -> sql
app -> execution
execution -> storage
execution -> index
index -> common
storage -> common
tests -> 각 모듈
benchmark -> execution / storage / index
```

중요 원칙:

- `sql`은 `index`를 직접 알지 않는다.
- `index`는 `sql`을 직접 알지 않는다.
- `app`은 `storage`와 `index`를 직접 조작하지 않는다.

## 10. 기존 코드와의 연결 지점

현재 코드 기준 주요 연결 지점은 아래다.

- `src/app/main.c`
  일반 SQL 처리기 CLI 진입점
- `src/benchmark/benchmark_main.c`
  벤치마크 전용 진입점
- `src/sql/parser.c`
  이미 `WHERE` AST 필드가 있으므로 `WHERE id = value` 표현 재사용
- `src/execution/executor.c`
  인덱스 경로와 선형 탐색 경로 분기 추가
- `src/storage/storage.c`
  append 후 오프셋 반환, 오프셋 기반 행 조회, CSV 전체 순회 함수 추가
- `src/storage/schema.c`
  스키마 로딩, CSV 헤더 검증, `id` 위치 조회 보조

## 11. 성능 테스트 구조

성능 테스트는 일반 CLI와 분리하는 것을 권장한다.

이유:

- 일반 SQL 실행 흐름과 벤치마크 흐름의 목적이 다르다.
- 데이터 준비 단계와 조회 측정 단계를 분리하면 발표 시연과 반복 측정이 쉬워진다.
- 측정 코드가 일반 사용자 흐름을 복잡하게 만들지 않게 할 수 있다.

권장 방식:

- `src/benchmark/benchmark_main.c` 추가
- 벤치마크는 전용 schema/data 작업 디렉터리에서 실행
- `prepare` 모드:
  - 특정 테이블에 1,000,000건 이상 레코드 생성
  - 같은 입력 파라미터로 기존 벤치마크 CSV를 헤더 기준으로 초기화한 뒤 같은 데이터셋을 다시 생성
- `query-only` 모드:
  - 이미 준비된 데이터셋을 그대로 사용
  - 같은 프로세스에서
    `WHERE id = ?`
    `WHERE other_column = ?`
    등의 질의를 반복 측정
- 발표 시연에서는 `prepare`로 미리 데이터셋을 만들어 두고 `query-only`로 조회 시간만 비교하는 흐름을 권장

## 12. 테스트 전략

### 12.1 단위 테스트

- B+ 트리 삽입
- B+ 트리 탐색
- B+ 트리 분할
- 테이블 인덱스 lazy load
- `next_id` 계산
- 오프셋 기반 행 조회
- 인덱스 등록 실패 이후 무효화 상태 확인

### 12.2 기능 테스트

- INSERT 후 자동 `id` 부여 확인
- INSERT 후 인덱스 등록 확인
- `WHERE id = value` 조회 결과 확인
- 비인덱스 WHERE 결과 확인
- 재시작 후 인덱스 재구성 확인

### 12.3 성능 테스트

- `prepare` 모드의 대량 삽입 시간
- `query-only` 모드의 인덱스 기반 단건 조회 시간
- `query-only` 모드의 선형 탐색 기반 단건 조회 시간
- `prepare` 모드를 같은 입력 파라미터로 재실행했을 때 동일 데이터셋 재생성 확인
- `query-only` 모드가 기존 데이터셋을 다시 초기화하지 않는지 확인

## 13. 아키텍처 요약

7주차 확장 아키텍처의 핵심은 아래 한 줄로 요약할 수 있다.

`기존 SqlParser 구조를 유지하고, execution 계층에서 ID 자동 부여와 인덱스 경로 선택을 담당하며, storage는 CSV 영속 저장을 유지하고, index 계층은 메모리 기반 B+ 트리만 책임진다.`
