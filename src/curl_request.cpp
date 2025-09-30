#include "curl_request.hpp"

#include <iostream>

namespace duckdb {

static
void dump(const char *text,
          FILE *stream, unsigned char *ptr, size_t size)
{
  size_t i;
  size_t c;
  unsigned int width = 0x10;
 
  fprintf(stream, "%s, %10.10ld bytes (0x%8.8lx)\n",
          text, (long)size, (long)size);
 
  for(i = 0; i < size; i += width) {
    fprintf(stream, "%4.4lx: ", (long)i);
 
    /* show hex to the left */
    for(c = 0; c < width; c++) {
      if(i + c < size)
        fprintf(stream, "%02x ", ptr[i + c]);
      else
        fputs("   ", stream);
    }
 
    /* show data on the right */
    for(c = 0; (c < width) && (i + c < size); c++) {
      char x = (ptr[i + c] >= 0x20 && ptr[i + c] < 0x80) ? ptr[i + c] : '.';
      fputc(x, stream);
    }
 
    fputc('\n', stream); /* newline */
  }
}

static
int my_trace(CURL *handle, curl_infotype type,
             char *data, size_t size,
             void *clientp)
{
  const char *text;
  (void)handle;
  (void)clientp;
 
  switch(type) {
  case CURLINFO_TEXT:
    fputs("== Info: ", stderr);
    fwrite(data, size, 1, stderr);
  default: /* in case a new one is introduced to shock us */
    return 0;
 
  case CURLINFO_HEADER_OUT:
    text = "=> Send header";
    break;
  case CURLINFO_DATA_OUT:
    text = "=> Send data";
    break;
  case CURLINFO_SSL_DATA_OUT:
    text = "=> Send SSL data";
    break;
  case CURLINFO_HEADER_IN:
    text = "<= Recv header";
    break;
  case CURLINFO_DATA_IN:
    text = "<= Recv data";
    break;
  case CURLINFO_SSL_DATA_IN:
    text = "<= Recv SSL data";
    break;
  }
 
  dump(text, stderr, (unsigned char *)data, size);
  return 0;
}

CurlRequest::CurlRequest(string url) : info(make_uniq<RequestInfo>()) {
	info->url = std::move(url);
	easy = curl_easy_init();
	if (easy == nullptr) {
		throw InternalException("Failed to initialize curl easy");
	}

	curl_easy_setopt(easy, CURLOPT_DEBUGFUNCTION, my_trace);
	curl_easy_setopt(easy, CURLOPT_URL, info->url.c_str());
	curl_easy_setopt(easy, CURLOPT_PROGRESSFUNCTION, CurlRequest::ProgressCallback);
	curl_easy_setopt(easy, CURLOPT_PROGRESSDATA, this);

	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, CurlRequest::WriteBody);
	curl_easy_setopt(easy, CURLOPT_WRITEDATA, this);

	curl_easy_setopt(easy, CURLOPT_PRIVATE, this);

	// For debugging purpose.
	curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
}

CurlRequest::~CurlRequest() {
	if (headers != nullptr) {
		curl_slist_free_all(headers);
	}
	if (easy != nullptr) {
		curl_easy_cleanup(easy);
	}
}

/*static*/ size_t CurlRequest::WriteBody(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t total_size = size * nmemb;
	auto *req = static_cast<CurlRequest *>(userp);
	req->info->body.append(static_cast<char *>(contents), total_size);
	if (req->get_info && req->get_info->content_handler) {
		req->get_info->content_handler(
		    const_data_ptr_cast(req->info->body.data() + req->info->body.size() - total_size), total_size);
	}
	return total_size;
}

/*static*/ int CurlRequest::ProgressCallback(void *p, double dltotal, double dlnow, double ult, double uln) {
	std::cerr << "Progress: " << dlnow << dltotal << std::endl;
	return 0;
}

} // namespace duckdb
