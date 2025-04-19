#include <curl/curl.h>

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

int main()
{
    CURL* curl = curl_easy_init();
    assert(curl);
    curl_easy_cleanup(curl);
}
