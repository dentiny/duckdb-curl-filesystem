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
