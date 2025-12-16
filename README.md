# ccurl_httpfs - Bağlantı havuzu, HTTP/2 ve asenkron ağ IO'su için DuckDB eklentisi

## curl_httpfs nedir?

Bağlantı havuzu, HTTP/2 ve asenkron ağ IO'su dahil olmak üzere, [httpfs eklentisi](https://duckdb.org/docs/stable/core_extensions/httpfs/overview.html) üzerine ek IO özellikleri sağlayan bir DuckDB eklentisidir.

`httpfs` eklentisi ile %100 uyumludur; tüm gelişmiş özellikler curl tabanlı çözüm kullanılarak uygulanmıştır, bununla birlikte kullanıcıların httplib'e geri dönmesine (fallback) de olanak tanır.

## Kullanım
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
