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
