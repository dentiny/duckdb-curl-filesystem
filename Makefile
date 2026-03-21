PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=curl_httpfs
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

format-all: format
	find integration/ -iname *.hpp -o -iname *.cpp | xargs clang-format --sort-includes=0 -style=file -i
	find benchmark/ -iname *.hpp -o -iname *.cpp | xargs clang-format --sort-includes=0 -style=file -i
	cmake-format -i CMakeLists.txt

HTTPFS_TEST_TMPDIR := /tmp/curl-httpfs-duckdb-httpfs-test

# httpfs tests to skip for curl_httpfs compatibility testing.
HTTPFS_TEST_BLACKLIST := \
	test/sql/logging/http_logging.test \
	test/sql/storage/external_file_cache/external_file_cache_httpfs.test \
	test/sql/storage/external_file_cache/external_file_cache_read_blob.test_slow

# Prepare httpfs tests: rewrite require directives, inject metrics clear, and remove blacklisted tests.
define PREPARE_HTTPFS_TESTS
	@rm -rf $(HTTPFS_TEST_TMPDIR)
	@mkdir -p $(HTTPFS_TEST_TMPDIR)
	@cp -r duckdb-httpfs/test $(HTTPFS_TEST_TMPDIR)/test
	@find $(HTTPFS_TEST_TMPDIR)/test -type f \( -name "*.test" -o -name "*.test_slow" \) -exec \
		sed -i 's/^require httpfs$$/require curl_httpfs\n\nstatement ok\nSELECT curl_httpfs_clear_metrics();/' {} +
	@for f in $(HTTPFS_TEST_BLACKLIST); do rm -f $(HTTPFS_TEST_TMPDIR)/$$f; done
	@rm -rf $(HTTPFS_TEST_TMPDIR)
endef

test_reldebug_httpfs:
	$(PREPARE_HTTPFS_TESTS)
	./build/reldebug/test/unittest --test-dir $(HTTPFS_TEST_TMPDIR) "test/sql/*"

test_release_httpfs:
	$(PREPARE_HTTPFS_TESTS)
	./build/release/test/unittest --test-dir $(HTTPFS_TEST_TMPDIR) "test/sql/*"

test_debug_httpfs:
	$(PREPARE_HTTPFS_TESTS)
	./build/debug/test/unittest --test-dir $(HTTPFS_TEST_TMPDIR) "test/sql/*"

.PHONY: format-all test_release_httpfs test_reldebug_httpfs test_debug_httpfs
