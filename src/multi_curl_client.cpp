#include "multi_curl_client.hpp"

#include "duckdb/common/numeric_utils.hpp"
#include "extension_config.hpp"
#include "multi_curl_manager.hpp"

namespace duckdb {

MultiCurlClient::MultiCurlClient(HTTPFSParams &http_params, const string &proto_host_port)
    : HTTPClient(proto_host_port) {
	Initialize(http_params);
}

void MultiCurlClient::Initialize(HTTPParams &http_p) {
	auto &params = http_p.Cast<HTTPFSParams>();
	auto &http_util = params.http_util.Cast<HTTPFSCurlUtil>();
	// Use the upstream client for its request handling and curl-global initialization.
	upstream_client = http_util.HTTPFSCurlUtil::InitializeClient(params, GetBaseUrl());
	state = params.state;
	curl = make_uniq<CURLHandle>(params.ca_cert_file.empty());
	ca_cert_bundle.reset();

	if (params.VerifyServerCertificate()) {
		if (HTTPFSUtil::IsSecureConnection(GetBaseUrl())) {
			ca_cert_bundle = http_util.GetCertificateBundle(params.ca_cert_file);
		} else {
			// A plain HTTP request may redirect to HTTPS, but need not have a certificate bundle.
			try {
				ca_cert_bundle = http_util.GetCertificateBundle(params.ca_cert_file);
			} catch (const std::exception &) {
				ca_cert_bundle.reset();
			}
		}
	}
	if (ca_cert_bundle) {
		curl_blob blob;
		blob.data = const_cast<char *>(ca_cert_bundle->data());
		blob.len = ca_cert_bundle->size();
		blob.flags = CURL_BLOB_NOCOPY;
		curl->SetOption(CURLOPT_CAINFO, nullptr);
		curl->SetOption(CURLOPT_CAINFO_BLOB, &blob);
	}
	if (!params.bearer_token.empty()) {
		curl->SetOption(CURLOPT_XOAUTH2_BEARER, params.bearer_token.c_str());
		curl->SetOption(CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
	}
	curl->SetOption(CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
	curl->SetOption(CURLOPT_FOLLOWLOCATION, params.follow_location ? 1L : 0L);
	curl->SetOption(CURLOPT_FORBID_REUSE, params.keep_alive ? 0L : 1L);
	const bool verify_ssl = params.VerifyServerCertificate();
	curl->SetOption(CURLOPT_SSL_VERIFYPEER, verify_ssl ? 1L : 0L);
	curl->SetOption(CURLOPT_SSL_VERIFYHOST, verify_ssl ? 2L : 0L);
	curl->SetOption(CURLOPT_TIMEOUT, NumericCast<long>(params.timeout));
	curl->SetOption(CURLOPT_CONNECTTIMEOUT, NumericCast<long>(params.timeout));
	curl->SetOption(CURLOPT_ACCEPT_ENCODING, "identity");
	curl->SetOption(CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
	if (!params.http_proxy.empty()) {
		curl->SetOption(CURLOPT_PROXY, StringUtil::Format("%s:%s", params.http_proxy, params.http_proxy_port).c_str());
		if (!params.http_proxy_username.empty()) {
			curl->SetOption(CURLOPT_PROXYUSERNAME, params.http_proxy_username.c_str());
			curl->SetOption(CURLOPT_PROXYPASSWORD, params.http_proxy_password.c_str());
		}
	}
}

unique_ptr<HTTPResponse> MultiCurlClient::Get(GetRequestInfo &info) {
	if (state) {
		state->RecordRequest(RequestType::GET_REQUEST);
	}
	auto request = make_uniq<CurlRequest>(*curl);
	request->SetUrl(info.url);
	request->SetHeaders(info.headers, info.params);
	request->SetGetAttrs();
	auto response = MultiCurlManager::GetInstance().HandleRequest(std::move(request));
	if (state) {
		state->RecordBytesReceived(response->body.size());
	}
	if (info.response_handler && !info.response_handler(*response)) {
		return response;
	}
	if (info.content_handler) {
		info.content_handler(const_data_ptr_cast(response->body.data()), response->body.size());
	}
	return response;
}

unique_ptr<HTTPResponse> MultiCurlClient::Head(HeadRequestInfo &info) {
	if (state) {
		state->RecordRequest(RequestType::HEAD_REQUEST);
	}
	auto request = make_uniq<CurlRequest>(*curl);
	request->SetUrl(info.url);
	request->SetHeaders(info.headers, info.params);
	request->SetHeadAttrs();
	return MultiCurlManager::GetInstance().HandleRequest(std::move(request));
}

unique_ptr<HTTPResponse> MultiCurlClient::Put(PutRequestInfo &info) {
	return upstream_client->Put(info);
}

unique_ptr<HTTPResponse> MultiCurlClient::Delete(DeleteRequestInfo &info) {
	return upstream_client->Delete(info);
}

unique_ptr<HTTPResponse> MultiCurlClient::Post(PostRequestInfo &info) {
	return upstream_client->Post(info);
}

unique_ptr<HTTPResponse> MultiCurlClient::Options(OptionsRequestInfo &info) {
	return upstream_client->Options(info);
}

} // namespace duckdb
