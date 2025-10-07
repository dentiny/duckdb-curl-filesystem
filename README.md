# curl_httpfs - DuckDB extension for connection pool, HTTP/2 and asynchronous network IO

## What is curl_httpfs?

It's a DuckDB extension that provides additional IO features upon [httpfs extension](https://duckdb.org/docs/stable/core_extensions/httpfs/overview.html), including connection pool, HTTP/2, and async network IO.

It's 100% compatible with `httpfs` extension, all advanced features are implemented based on uses curl-based solution, meanwhile it also allows users to fallback to httplib.

## Usage
```sql
-- Install and load the curl_httpfs extension
FORCE INSTALL curl_httpfs FROM community;
LOAD curl_httpfs;

-- Users could access file as usual.
D SELECT length(content) AS char_count FROM read_text('https://raw.githubusercontent.com/dentiny/duck-read-cache-fs/main/test/data/stock-exchanges.csv');
┌────────────┐
│ char_count │
│   int64    │
├────────────┤
│   16205    │
└────────────┘

-- Switch back to httplib.
D SET httpfs_client_implementation='httplib';

-- Switch back to curl.
D SET httpfs_client_implementation='curl';
```
