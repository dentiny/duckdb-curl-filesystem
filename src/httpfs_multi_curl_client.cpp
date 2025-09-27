#include "httpfs_client.hpp"
#include "http_state.hpp"
#include "multicurl_engine.hpp"

#define CPPHTTPLIB_OPENSSL_SUPPORT

#include <curl/curl.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <atomic>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <chrono>
#include "duckdb/common/exception/http_exception.hpp"

#define CHECK_CURL_OK(expr)  D_ASSERT((expr) == CURLE_OK)
#define CHECK_CURLM_OK(expr) D_ASSERT((expr) == CURLM_OK)

namespace duckdb {

// =====================================================================================
// CA bundle selection (unchanged)
// =====================================================================================
static std::string certFileLocations[] = {
    // Arch, Debian-based, Gentoo
    "/etc/ssl/certs/ca-certificates.crt",
    // RedHat 7 based
    "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
    // Redhat 6 based
    "/etc/pki/tls/certs/ca-bundle.crt",
    // OpenSUSE
    "/etc/ssl/ca-bundle.pem",
    // Alpine
    "/etc/ssl/cert.pem",
};

static std::string SelectCURLCertPath() {
	for (std::string &caFile : certFileLocations) {
		struct stat buf;
		if (stat(caFile.c_str(), &buf) == 0) {
			return caFile;
		}
	}
	return std::string();
}

static std::atomic<idx_t> httpfs_client_count {0};

// Helpers identical to your previous callbacks, but tied to EasyRequest above
// (kept for readability)
static size_t RequestWriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
	return EasyRequest::WriteBody(contents, size, nmemb, userp);
}
static size_t RequestHeaderCallback(void *contents, size_t size, size_t nmemb, void *userp) {
	return EasyRequest::WriteHeader(contents, size, nmemb, userp);
}

class HTTPFSCurlClient : public HTTPClient {
public:
	HTTPFSCurlClient(HTTPFSParams &http_params, const string & /*proto_host_port*/) {
		state = http_params.state;

		// global init once
		InitCurlGlobal();

		ca_cert_path_ = SelectCURLCertPath();

		timeout_seconds_ = http_params.timeout;
		verify_peer_ = http_params.enable_curl_server_cert_verification;
		keep_alive_ = http_params.keep_alive;
		http_proxy_ = http_params.http_proxy;
		http_proxy_port_ = http_params.http_proxy_port;
		http_proxy_user_ = http_params.http_proxy_username;
		http_proxy_pass_ = http_params.http_proxy_password;
		bearer_token_ = http_params.bearer_token;
	}

	~HTTPFSCurlClient() override {
		DestroyCurlGlobal();
	}

	unique_ptr<HTTPResponse> Get(GetRequestInfo &info) override {
		if (state)
			state->get_count++;
		EasyRequest req;
		ConfigureCommon(req, "GET", info.url, info.headers, info.params);
		req.get_info = &info; // enable streaming callback if provided
		return MultiCurlEngine::Instance().Perform(&req);
	}

	unique_ptr<HTTPResponse> Head(HeadRequestInfo &info) override {
		if (state)
			state->head_count++;
		EasyRequest req;
		ConfigureCommon(req, "HEAD", info.url, info.headers, info.params);
		// HEAD specifics
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_NOBODY, 1L));
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_HTTPGET, 0L));
		return MultiCurlEngine::Instance().Perform(&req);
	}

	unique_ptr<HTTPResponse> Delete(DeleteRequestInfo &info) override {
		if (state)
			state->delete_count++;
		EasyRequest req;
		ConfigureCommon(req, "DELETE", info.url, info.headers, info.params);
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_CUSTOMREQUEST, "DELETE"));
		return MultiCurlEngine::Instance().Perform(&req);
	}

	unique_ptr<HTTPResponse> Post(PostRequestInfo &info) override {
		if (state) {
			state->post_count++;
			state->total_bytes_sent += info.buffer_in_len;
		}
		EasyRequest req;
		ConfigureCommon(req, "POST", info.url, info.headers, info.params);
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_POST, 1L));
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_POSTFIELDS, const_char_ptr_cast(info.buffer_in)));
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_POSTFIELDSIZE, info.buffer_in_len));
		// default content-type if not provided by caller headers
		if (!HasContentType(info.headers)) {
			auto content_type = std::string("Content-Type: application/octet-stream");
			req.headers = curl_slist_append(req.headers, content_type.c_str());
			CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_HTTPHEADER, req.headers));
		}
		auto resp = MultiCurlEngine::Instance().Perform(&req);
		info.buffer_out = req.ri->body;
		return resp;
	}

	unique_ptr<HTTPResponse> Put(PutRequestInfo &info) override {
		if (state) {
			state->put_count++;
			state->total_bytes_sent += info.buffer_in_len;
		}
		EasyRequest req;
		ConfigureCommon(req, "PUT", info.url, info.headers, info.params);
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_CUSTOMREQUEST, "PUT"));
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_POSTFIELDS, const_char_ptr_cast(info.buffer_in)));
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_POSTFIELDSIZE, info.buffer_in_len));

		// Ensure content-type present
		std::string ct = "Content-Type: " + info.content_type;
		req.headers = curl_slist_append(req.headers, ct.c_str());
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_HTTPHEADER, req.headers));

		return MultiCurlEngine::Instance().Perform(&req);
	}

private:
	void ConfigureCommon(EasyRequest &req, const char * /*method*/, const std::string &base_url,
	                     const HTTPHeaders &headers, const HTTPParams &params) {
		// URL & query params
		req.ri->url = base_url;
		if (!params.extra_headers.empty()) {
			std::string q = TransformParamsCurl(req, params);
			if (!q.empty()) {
				req.ri->url += (req.ri->url.find('?') == std::string::npos ? "?" : "&");
				req.ri->url += q;
			}
		}

		// headers
		for (auto &kv : headers) {
			std::string h = kv.first + ": " + kv.second;
			req.headers = curl_slist_append(req.headers, h.c_str());
		}
		// Bearer token
		if (!bearer_token_.empty()) {
			CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_XOAUTH2_BEARER, bearer_token_.c_str()));
			CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_HTTPAUTH, CURLAUTH_BEARER));
		}
		// CA bundle and TLS
		if (!ca_cert_path_.empty()) {
			CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_CAINFO, ca_cert_path_.c_str()));
		}
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_SSL_VERIFYPEER, verify_peer_ ? 1L : 0L));
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_SSL_VERIFYHOST, verify_peer_ ? 2L : 0L));

		// timeouts
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_TIMEOUT, timeout_seconds_));
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_CONNECTTIMEOUT, timeout_seconds_));

		// encoding
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_ACCEPT_ENCODING, "identity"));

		// redirects
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_FOLLOWLOCATION, 1L));

		// reuse / keepalive
		if (!keep_alive_) {
			CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_FORBID_REUSE, 1L));
		}

		// callbacks
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_WRITEFUNCTION, RequestWriteCallback));
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_WRITEDATA, &req));

		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_HEADERFUNCTION, RequestHeaderCallback));
		CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_HEADERDATA, &req));

		// apply headers
		if (req.headers) {
			CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_HTTPHEADER, req.headers));
		}

		// proxy
		if (!http_proxy_.empty()) {
			std::string proxy = StringUtil::Format("%s:%s", http_proxy_, http_proxy_port_);
			CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_PROXY, proxy.c_str()));
			if (!http_proxy_user_.empty()) {
				CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_PROXYUSERNAME, http_proxy_user_.c_str()));
				CHECK_CURL_OK(curl_easy_setopt(req.easy, CURLOPT_PROXYPASSWORD, http_proxy_pass_.c_str()));
			}
		}

		// Inform multi socket callback which EasyRequest this socket belongs to
		CHECK_CURLM_OK(curl_multi_assign(MultiCurlEngine::Instance().MultiHandle(), CURL_SOCKET_TIMEOUT, &req));
		// Also set per-socket pointer via CURLOPT_OPENSOCKETFUNCTION if needed (not required here).
	}

	static bool HasContentType(const HTTPHeaders &headers) {
		for (auto &kv : headers) {
			if (!strcasecmp(kv.first.c_str(), "content-type"))
				return true;
		}
		return false;
	}

	static string TransformParamsCurl(EasyRequest &req, const HTTPParams &params) {
		string result;
		bool first = true;
		for (auto &entry : params.extra_headers) {
			const string &key = entry.first;
			char *esc = curl_easy_escape(req.easy, entry.second.c_str(), 0);
			if (!esc)
				continue;
			if (!first)
				result += "&";
			result += key + "=" + std::string(esc);
			curl_free(esc);
			first = false;
		}
		return result;
	}

private:
	optional_ptr<HTTPState> state;

	std::string ca_cert_path_;
	long timeout_seconds_ {0};
	bool verify_peer_ {true};
	bool keep_alive_ {true};
	std::string http_proxy_;
	std::string http_proxy_port_;
	std::string http_proxy_user_;
	std::string http_proxy_pass_;
	std::string bearer_token_;

	// Global refcount to init/cleanup once
	static void InitCurlGlobal() {
		if (httpfs_client_count++ == 0) {
			MultiCurlEngine::GlobalInit();
		}
	}
	static void DestroyCurlGlobal() {
		// We intentionally do not call curl_global_cleanup each client; see engine.
		D_ASSERT(httpfs_client_count > 0);
		if (--httpfs_client_count == 0) {
			MultiCurlEngine::GlobalCleanup();
		}
	}
};

} // namespace duckdb
