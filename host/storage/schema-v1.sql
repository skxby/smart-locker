-- schema-v1.sql -- S1-02 唯一 DDL 真源（SQLite 文档存储模型，契约 0.2.1）
--
-- 设计要点（对应 contracts/internal-data.schema.json 的 x-rules 01/03/07/12/13）：
--   * 记录以 JSON 文本存放在 documents(collection,key) 中，字段由契约 schema 冻结，
--     不允许把记录字段映射成 SQL 列，也不允许把 key/value 拼进 SQL（全部参数绑定）。
--   * documents.key 使用 BINARY 排序（SQLite 默认 TEXT 排序即字节序），分页按 key 字节序。
--   * u64 一律以「已校验的十进制文本」保存（U64 pattern），禁止用 SQLite 数值比较或字典序比较；
--     数值比较在 C 侧按 uint64 进行（见 storage_u64.c）。
--   * 设备事件键按 device_id/epoch/20 位补零 boot/20 位补零 seq 拼接为 documents.key，
--     因此 BINARY 排序即时间序（2 排在 10 之前）。
--   * meta 表保存 schema_version 与 snapshot_revision；migrations 表保存迁移元数据。
--   * 本文件只被 CMake 在构建期读取并嵌入库内（host/storage/src/schema_embedded.c），
--     运行时不再从磁盘加载 DDL，避免部署目录漂移。
--   * 初始化失败（损坏/版本不兼容）一律不清库、不重建；见 repository.c。

PRAGMA foreign_keys = ON;

-- 元数据：schema_version、snapshot_revision、host_instance_id 等标量。
CREATE TABLE IF NOT EXISTS meta (
    name  TEXT PRIMARY KEY NOT NULL,
    value TEXT NOT NULL
) WITHOUT ROWID;

-- 迁移元数据：版本单调递增，已应用版本不可重复应用，不自动删库重建。
CREATE TABLE IF NOT EXISTS migrations (
    version    INTEGER PRIMARY KEY NOT NULL,
    applied_at TEXT NOT NULL,
    checksum   TEXT NOT NULL
);

-- 文档存储：18 个受控集合共用一张表，(collection,key) 唯一。
CREATE TABLE IF NOT EXISTS documents (
    collection TEXT NOT NULL,
    key        TEXT NOT NULL,
    value_json TEXT NOT NULL,
    PRIMARY KEY (collection, key)
) WITHOUT ROWID;

-- 初始化标记：只有本机 CLI（initialize_empty=true）才会写入。
INSERT OR IGNORE INTO meta(name, value) VALUES ('schema_version', '1');
INSERT OR IGNORE INTO meta(name, value) VALUES ('snapshot_revision', '0');
INSERT OR IGNORE INTO migrations(version, applied_at, checksum)
    VALUES (1, '1970-01-01T00:00:00Z', 'schema-v1');
