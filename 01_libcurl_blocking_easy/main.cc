#include <print>
#include <string>

#include <curl/curl.h>

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

// our API
std::string CURL_get(const std::string& url);

static size_t CURL_OnWriteCallback(void* ptr, size_t size, size_t nmemb, void* data)
{
    std::string& response = *static_cast<std::string*>(data);
    response.append(static_cast<const char*>(ptr), size * nmemb);
    return (size * nmemb);
}

std::string CURL_get(const std::string& url)
{
    CURL* curl = curl_easy_init();
    assert(curl);
    assert(CURLE_OK == curl_easy_setopt(curl, CURLOPT_URL, url.c_str()));
    assert(CURLE_OK == curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L));
    
    std::string response;
    assert(CURLE_OK == curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CURL_OnWriteCallback));
    assert(CURLE_OK == curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response));

    assert(CURLE_OK == curl_easy_perform(curl));

    long response_code = -1;
    assert(CURLE_OK == curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code));
    assert(response_code == 200L);

    curl_easy_cleanup(curl);
    return response;
}

int main()
{
    const std::string r = CURL_get("localhost:5001/file1.txt");
    std::println("CURL_get(file1.txt): '{}'", r);
}
