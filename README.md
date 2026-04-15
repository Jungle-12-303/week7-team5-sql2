# SQL Processor Week 7

이 프로젝트는 C로 작성한 파일 기반 SQL 처리기입니다. CSV를 영속 저장소로 사용하고, 7주차 과제 목표에 맞춰 `id` 자동 부여와 메모리 기반 B+ 트리 인덱스를 추가했습니다.

기준 문서는 [docs/architecture.md](/C:/developer_folder/jungle-sql-processor-2nd/docs/architecture.md)와 [docs/requirements.md](/C:/developer_folder/jungle-sql-processor-2nd/docs/requirements.md)입니다. 구현과 사용법은 이 README보다 두 문서를 우선합니다.

## 핵심 기능

- `INSERT`
- `SELECT *`
- `SELECT column1, column2`
- `SELECT ... WHERE column = value`
- `WHERE id = <number>` 인덱스 조회
- `id` 자동 부여
- SQL 파일 실행
- SQL 문자열 직접 실행
- REPL 실행
- Docker/Linux 기준 빌드와 테스트
- 별도 벤치마크 바이너리

## 현재 제외 범위

- `UPDATE`
- `DELETE`
- `JOIN`
- `ORDER BY`
- `GROUP BY`
- 복합 `WHERE`
- 범위 검색 최적화
- 디스크 기반 B+ 트리

## 실행 구조

전체 흐름은 아래와 같습니다.

`input -> lexer -> parser -> executor -> storage/index -> output`

주요 디렉터리:

- `src/app`
  CLI 입력, 파일 입력, REPL 시작점
- `src/sql`
  lexer, parser, AST
- `src/execution`
  `INSERT`/`SELECT` 실행 분기, `id` 자동 생성, 인덱스 경로 선택
- `src/storage`
  스키마 로딩, CSV 검증, CSV 읽기/쓰기, CSV 순회
- `src/index`
  B+ 트리와 테이블별 인덱스 관리
- `src/benchmark`
  대량 삽입 및 조회 성능 측정용 별도 진입점
- `tests`
  단위/기능 테스트
- `docs`
  요구사항, 아키텍처, 테스트 문서

## 데이터 형식

테이블은 아래 두 파일이 모두 있어야 합니다.

- `schema/<storage_name>.meta`
- `data/<storage_name>.csv`

예시:

```txt
table=학생
columns=id,department,student_number,name,age
```

같은 테이블의 CSV 첫 줄은 아래와 같이 헤더를 가져야 합니다.

```txt
id,department,student_number,name,age
```

현재 포함된 샘플 데이터는 아래입니다.

- [schema/student.meta](/C:/developer_folder/jungle-sql-processor-2nd/schema/student.meta)
- [data/student.csv](/C:/developer_folder/jungle-sql-processor-2nd/data/student.csv)

## SQL 동작 규칙

### INSERT

- 사용자는 `id`를 빼고 나머지 컬럼만 넣어도 됩니다.
- 최종 저장되는 `id`는 시스템이 자동으로 결정합니다.
- 새 `id`는 현재 테이블의 최대 `id + 1`입니다.
- 저장 성공 후 `id -> CSV 행 위치`가 메모리 B+ 트리에 등록됩니다.

예시:

```sql
INSERT INTO 학생 (department, student_number, name, age)
VALUES ('컴퓨터공학과', '2024001', '김민수', 20);
```

### SELECT

- 일반 `SELECT`와 일반 `WHERE`는 기존 CSV 선형 탐색을 사용합니다.
- `WHERE id = <number>`는 정수 검증 후 B+ 트리 인덱스를 사용합니다.
- `WHERE id = abc` 같은 입력은 오류입니다.

예시:

```sql
SELECT * FROM 학생;
SELECT name, age FROM 학생 WHERE department = '컴퓨터공학과';
SELECT * FROM 학생 WHERE id = 1000;
```

## 빌드

Linux 또는 Docker 기준:

```bash
make all
```

테스트 바이너리:

```bash
make test
```

벤치마크 바이너리:

```bash
make benchmark
```

## CLI 사용법

도움말:

```bash
./build/bin/sqlparser --help
```

SQL 문자열 직접 실행:

```bash
./build/bin/sqlparser -e "SELECT * FROM 학생;"
```

SQL 파일 실행:

```bash
./build/bin/sqlparser -f path/to/query.sql
```

표준입력으로 실행:

```bash
echo "SELECT name FROM 학생;" | ./build/bin/sqlparser
```

REPL 실행:

```bash
./build/bin/sqlparser
```

REPL 종료 명령:

- `.exit`
- `.quit`
- `exit`
- `quit`

## Docker 사용 예시

이미지 빌드:

```bash
docker build -t jungle-sql-processor-test .
```

CLI 실행:

```bash
docker run --rm -it -v "C:/developer_folder/jungle-sql-processor-2nd:/workspace" -w /workspace jungle-sql-processor-test ./build/bin/sqlparser
```

테스트 실행:

```bash
docker run --rm -v "C:/developer_folder/jungle-sql-processor-2nd:/workspace" -w /workspace jungle-sql-processor-test bash scripts/docker-test.sh
```

## 테스트

현재 테스트 러너 기준:

- 상위 테스트 함수 `29개`
- assertion `339개`

실행:

```bash
bash scripts/docker-test.sh
```

또는:

```bash
make test
```

테스트 범위 요약은 [docs/test-cases.md](/C:/developer_folder/jungle-sql-processor-2nd/docs/test-cases.md)에 정리돼 있습니다.

## 벤치마크

벤치마크는 별도 바이너리로 실행합니다.

```bash
./build/bin/benchmark_runner <schema_dir> <data_dir> <table_name> <row_count> [query_repeat]
```

예시:

```bash
./build/bin/benchmark_runner benchmark-workdir/schema benchmark-workdir/data student 1000000 100
```

출력:

- 삽입한 행 수
- 전체 삽입 시간
- 반복 조회 횟수
- `WHERE id = ...` 인덱스 조회 평균 시간
- 일반 컬럼 `WHERE ...` 선형 조회 평균 시간

주의:

- 벤치마크는 시작 시 지정한 CSV를 헤더만 남기고 초기화한 뒤 같은 입력 파라미터로 같은 데이터셋을 다시 생성합니다.
- 실데이터를 보호하려면 기본 샘플인 `benchmark-workdir/schema`, `benchmark-workdir/data`에서 실행하는 것이 좋습니다.
- 기본 벤치마크 작업 디렉터리 샘플은 아래에 포함돼 있습니다.
  - [benchmark-workdir/schema/student.meta](/C:/developer_folder/jungle-sql-processor-2nd/benchmark-workdir/schema/student.meta)
  - [benchmark-workdir/data/student.csv](/C:/developer_folder/jungle-sql-processor-2nd/benchmark-workdir/data/student.csv)
- 현재 벤치마크는 실행 경로는 구현돼 있지만, 대량 데이터 성능 결과를 자동 검증하는 테스트는 아직 없습니다.

## 현재 상태 요약

- 7주차 핵심인 `id` 자동 부여와 `WHERE id` 인덱스 조회는 구현돼 있습니다.
- 일반 `WHERE`는 기존 선형 탐색을 유지합니다.
- 인덱스는 메모리 기반이며, 필요 시 CSV를 다시 읽어 재구성합니다.
- 인덱스 재구성 중 중복 `id`, 누락된 `id`, 정수가 아닌 `id`는 오류 처리합니다.

## 참고 문서

- [docs/requirements.md](/C:/developer_folder/jungle-sql-processor-2nd/docs/requirements.md)
- [docs/architecture.md](/C:/developer_folder/jungle-sql-processor-2nd/docs/architecture.md)
- [docs/test-cases.md](/C:/developer_folder/jungle-sql-processor-2nd/docs/test-cases.md)
