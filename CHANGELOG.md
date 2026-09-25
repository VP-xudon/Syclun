# Changelog / 更新日志

> A running, version-less log of standard-library additions and changes. The
> language is pre-1.0, so this file records *what changed* without version
> numbers or dates; see "Stability labels" in the standard-library reference for
> the guarantees each library currently makes.
> 一份**不含版本号与日期**的标准库新增/变更运行日志。语言尚处 1.0 之前，故本文件
> 只记录*变更内容*；各库当前所作的保证见标准库参考中的「稳定性分级」。

## Standard library / 标准库

- Added the `json` library (`&json;`): a dependency-free recursive-descent JSON
  codec — `json::Json.parse` / `json::Json.stringify` / `json::Json.pretty`,
  mapping JSON values onto native `std::Dict` / `std::Array` / `std::String` /
  `std::Number` / `std::Boolean` / `std::Object` (null).
  新增 `json` 库（`&json;`）：零依赖递归下降 JSON 编解码器——`json::Json.parse` /
  `json::Json.stringify` / `json::Json.pretty`，把 JSON 值映射为原生 `std::Dict` /
  `std::Array` / `std::String` / `std::Number` / `std::Boolean` / `std::Object`(null)。
- Added the `encoding` library (`&encoding;`): `encoding::Encoding.base64_encode` /
  `encoding::Encoding.base64_decode`, `encoding::Encoding.hex` /
  `encoding::Encoding.unhex`, `encoding::Encoding.url_encode` /
  `encoding::Encoding.url_decode` (RFC 4648 / RFC 3986).
  新增 `encoding` 库（`&encoding;`）：`encoding::Encoding.base64_encode` /
  `encoding::Encoding.base64_decode`、`encoding::Encoding.hex` /
  `encoding::Encoding.unhex`、`encoding::Encoding.url_encode` /
  `encoding::Encoding.url_decode`（RFC 4648 / RFC 3986）。
- `re::Match` now also exposes `char_start` / `char_end` — UTF-8 code-point
  offsets parallel to the existing byte offsets `start` / `end`.
  `re::Match` 现额外提供 `char_start` / `char_end`——与字节偏移 `start` / `end`
  平行的 UTF-8 码点偏移。
- `File` gained `push` / `push_line` / `format`, mirroring `io::OStream`, so a file
  can be written through the same streaming vocabulary (each call appends in binary
  mode: `push` adds no newline, `push_line` adds one, `format` fills `{ }`
  placeholders from an `Array`).
  `File` 新增 `push` / `push_line` / `format`，与 `io::OStream` 对齐，使文件也能用同一套
  流式写法（每次调用以二进制模式追加：`push` 不换行、`push_line` 换行、`format` 用 `Array`
  填充 `{ }` 占位符）。
- `warning::Raise` now stores `code` / `message` on the instance (and supports
  `=:` copy), so callers can query a raised warning instead of only seeing stderr.
  `warning::Raise` 现把 `code` / `message` 存到实例（并支持 `=:` 拷贝），调用方得以
  查询已发出的警告，而非仅见 stderr 输出。

## Governance / 治理

- Introduced stability labels: `error`, `warning`, and `sysapi` are marked
  **experimental**; everything else in this log is treated as stable for the
  current line.
  引入稳定性分级：`error`、`warning`、`sysapi` 标记为**实验性**；本日志其余项在当前
  版本线内视为稳定。
- Documented every library's exact method shape in the standard-library reference
  (`docs/olddocs/Syclun标准库参考.md`).
  在标准库参考（`docs/olddocs/Syclun标准库参考.md`）中逐库记录了精确的方法形态。
