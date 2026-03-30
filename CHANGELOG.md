# 0.4.0

## Changed

- Upgrade DuckDB core, extension-ci-tools and httpfs extension to v1.5.1 ([#63])

[#63]: https://github.com/dentiny/duckdb-curl-filesystem/pull/63

## Fixed

- Fix a few incompatibility with httpfs extension curl implementation ([#62])

[#62]: https://github.com/dentiny/duckdb-curl-filesystem/pull/62

- Override DuckDB Mtls TLS with openssl ([#60])

[#60]: https://github.com/dentiny/duckdb-curl-filesystem/pull/60

# 0.3.0

## Changed

- Rewrite extension with DuckDB http util interface, so we don't need to hard-fork httpfs extension ([#52])

[#52]: https://github.com/dentiny/duckdb-curl-filesystem/pull/52

- Implement compatibility with httpfs extension ([#53])

[#53]: https://github.com/dentiny/duckdb-curl-filesystem/pull/53

# 0.2.5

## Changed

- Upgrade DuckDB and extension-ci-tools to v1.5.0

# 0.2.4

## Changed

- Upgrade duckdb to v1.4.4

# 0.2.3

- Upgrade duckdb and extension-ci-tools to v1.4.3

# 0.2.2

## Add

- Provide option to enable verbose logging ([#34])

[#34]: https://github.com/dentiny/duckdb-curl-filesystem/pull/34

## Changed

- Upgrade duckdb and extension-ci-tools to v1.4.2 ([#40])

[#40]: https://github.com/dentiny/duckdb-curl-filesystem/pull/40

- Enable IO multiplexing via libcurl ([#37])

[#37]: https://github.com/dentiny/duckdb-curl-filesystem/pull/37

# 0.2.1

## Changed

- Upgrade duckdb to v1.4.1, and upgrade extension-ci to latest

# 0.2.0

## Added

- Support MacOs with `kqueue` as polling engine ([#27])

[#27]: https://github.com/dentiny/duckdb-curl-filesystem/pull/27

## Improved

- Use eventfd for new IO requests notification ([#22])

[#22]: https://github.com/dentiny/duckdb-curl-filesystem/pull/22
