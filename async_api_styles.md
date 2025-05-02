---
title: Styles of Asynchronous API
include-before: |

    Showcases different variations of asynchronous APIs with examples
    of using libcurl, specifically, doing 2 GET requests -
    both sequentially and concurrently.

    NO threads and/or multithreading involved to disconnect any associations
    of coroutines or fibers with threads. Something is intentionally
    simpler, while still having as much details as possible.

    Jump to [tasks](#tasks), [std::future](#future),
    [coroutines](#coroutines), [fibers](#fibers), [senders](#senders).
---

--------------------------------------------------------------------------------

# introduction {#intro}

Lets begin with simple C-style API on top of [libcurl C API](https://curl.se/libcurl/c/)
and build a program that may look like this:

``` cpp {.numberLines}
// our CURL API
std::string CURL_get(const std::string& url);

int main()
{
    const std::string r1 = CURL_get("localhost:5001/file1.txt");
    const std::string r2 = CURL_get("localhost:5001/file2.txt");
    return int(r1.size() + r2.size()); // handle results
}
```

This performs two GET requests sequentially. Everything executes synchronously.

Next, lets have idiomatic C-style callbacks API (note, intentionally,
**not** C++ one for now) to run requests concurrently:

``` cpp {.numberLines}
// libcurl bookkeeping
using CURL_Async = void*;
CURL_Async CURL_async_create();
void CURL_async_destroy(CURL_Async curl_async);
void CURL_async_tick(CURL_Async curl_async);

// main async callback API
void CURL_async_get(CURL_Async curl_async
    , const std::string& url
    , void* user_data
    , void (*callback)(void* user_data, std::string response));
```

Doing 2 GET requests is more involved now:

``` cpp {.numberLines}
int main()
{
    struct State
    {
        int count = 0;
        std::string r1;
        std::string r2;
    };

    CURL_Async curl_async = CURL_async_create();
    State state;
    CURL_async_get(curl_async, "localhost:5001/file1.txt", &state
        , [](void* user_data, std::string response)
    {
        State& state = *static_cast<State*>(user_data);
        state.count += 1;
        state.r1 = std::move(response);
    });
    CURL_async_get(curl_async, "localhost:5001/file2.txt", &state
        , [](void* user_data, std::string response)
    {
        State& state = *static_cast<State*>(user_data);
        state.count += 1;
        state.r2 = std::move(response);
    });
    while (state.count != 2) // wait for 2 requests to finish
    {
        CURL_async_tick(curl_async);
    }
    CURL_async_destroy(curl_async);
    return int(state.r1.size() + state.r2.size());
}
```

There is a need to have a `State` for bookkeeping, pass it as 
`void*` user data to access later and, finally, run an event loop
to give libcurl a chance to process requests. Note, however,
requests execute concurrently now, as in 2 requests are active at the same time.

After this, lets build [tasks](#tasks), [std::future](#future),
[coroutines](#coroutines), [fibers](#fibers), [senders](#senders) and other
variations of asynchronous API on top of C-style callbacks above.

But before that, lets wrap [libcurl C API](https://curl.se/libcurl/c/)
for our needs.

--------------------------------------------------------------------------------

# wrapping libcurl {#wrap_libcurl}

## cmake with libcurl and vcpkg setup {#cmake}

Source code: [main.cc](https://github.com/grishavanika/async_api_styles/blob/main/00_cmake_libcurl/main.cc),
[CMakeLists.txt](https://github.com/grishavanika/async_api_styles/blob/main/00_cmake_libcurl/CMakeLists.txt).

For [vcpkg](https://github.com/microsoft/vcpkg), there is extensive
[documentation](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started)
available. In short:

``` bash {.numberLines}
git clone https://github.com/microsoft/vcpkg
cd vcpkg
bootstrap-vcpkg.bat
:: for a later use, assume we are in `K:\vcpkg`
set VCPKG_ROOT=K:\vcpkg
:: to make `vcpkg` available
set path=%VCPKG_ROOT%;%PATH%
```

For the project (which is going to have `async_api_styles` name),
vcpkg [manifest mode](https://learn.microsoft.com/vcpkg/consume/manifest-mode)
is used. Together with `curl` setup, all required steps are:

``` bash {.numberLines}
cd async_api_styles
vcpkg new --application
vcpkg add port curl
```

Note: to find exact `curl` package name, `vcpkg search curl` was used which
prints:

> curl    8.13.0#1    A library for transferring data with URLs

[CMakeLists.txt](https://github.com/grishavanika/async_api_styles/blob/main/00_cmake_libcurl/CMakeLists.txt)
now looks like this:

``` cmake {.numberLines}
cmake_minimum_required(VERSION 3.24 FATAL_ERROR)
project(async_api_styles LANGUAGES CXX)

add_executable(00_cmake_libcurl main.cc)
find_package(CURL REQUIRED)
target_link_libraries(00_cmake_libcurl PRIVATE CURL::libcurl)
```

`find_package(CURL REQUIRED)` syntax together with `CURL::libcurl`
target name is found from the output log of `vcpkg install curl` (or during
CMake configuration run) which prints:

``` {.numberLines}
curl is compatible with built-in CMake targets:

    find_package(CURL REQUIRED)
    target_link_libraries(main PRIVATE CURL::libcurl)
```

To test that everything compiles and links, save [main.cc](https://github.com/grishavanika/async_api_styles/blob/main/00_cmake_libcurl/main.cc):

``` cpp {.numberLines}
#include <curl/curl.h>

int main()
{
    CURL* curl = curl_easy_init();
    assert(curl);
    curl_easy_cleanup(curl);
}
```

Finally, to invoke CMake configure, build and run (with vcpkg):

``` bash {.numberLines}
cd async_api_styles
cmake -S . -B build ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Debug
:: run test
./build/Debug/00_cmake_libcurl.exe
```

see [build.cmd](https://github.com/grishavanika/async_api_styles/blob/main/build.cmd).

## libcurl, blocking API with easy interface {#libcurl_easy}

Source code: [main.cc](https://github.com/grishavanika/async_api_styles/blob/main/01_libcurl_blocking_easy/main.cc).

libcurl comes with two different APIs,
["easy" and "multi"](https://curl.se/libcurl/c/). For now,
to have simplest blockig API, lets use easy interface; libcurl examples
available online, including official [simple.c example](https://curl.se/libcurl/c/simple.html)
for a start.

Our function should look like this:

``` cpp {.numberLines}
std::string CURL_get(const std::string& url);
```

which is translated to the implementation below, where `curl_easy_perform()`
call is the main one that blocks the execution until request complete:

``` cpp {.numberLines}
#include <string>

#include <curl/curl.h>

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

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

    CURLcode status = curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    assert(status == CURLE_OK);
    status = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    assert(status == CURLE_OK);
    
    std::string response;
    status = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CURL_OnWriteCallback);
    assert(status == CURLE_OK);
    status = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    assert(status == CURLE_OK);

    status = curl_easy_perform(curl);
    assert(status == CURLE_OK);

    long response_code = -1;
    status = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    assert(status == CURLE_OK);
    assert(response_code == 200L);

    curl_easy_cleanup(curl);
    return response;
}
```

Note on error handling: for now, we crash on any unexpected error - as in
"crash the whole application". In the [sample project](https://github.com/grishavanika/async_api_styles/blob/e8ed87380d0d739665b1e95570bdffa8de093c54/00_cmake_libcurl/main.cc#L3-L6),
`assert()` is enabled always **intentionally** to simplify both, the sample
code **and** debugging:

``` cpp {.numberLines}
// after all includes, main.cc
#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>
```

This is "bad" for generic, low-level library API/code, but could be
fine sometimes. We'll [discuss error handling later](#error_handling).

To see the code in action, lets run our program:

``` cpp {.numberLines}
#include <print>

int main()
{
    const std::string r = CURL_get("localhost:5001/file1.txt");
    std::println("CURL_get(file1.txt): '{}'", r);
}
```

that.. should crash since we don't have local HTTP server running to serve
`localhost:5001/file1.txt`. See the [next section on how to make it happen](#serve).

Once done, we should see the sample [file1.txt](https://github.com/grishavanika/async_api_styles/blob/main/01_libcurl_blocking_easy/file1.txt)
content in the console output:

```
CURL_get(file1.txt): 'content 1'

```

## python, run simple http server for tests {#serve}

To run sample code, lets use Python to have simple HTTP server that hosts
files in the current directory, see [serve.cmd](https://github.com/grishavanika/async_api_styles/blob/main/01_libcurl_blocking_easy/serve.cmd):

``` bash {.numberLines}
python -m http.server 5001
```

Given the directory that has [file1.txt](https://github.com/grishavanika/async_api_styles/blob/main/01_libcurl_blocking_easy/file1.txt)
and [file2.txt](https://github.com/grishavanika/async_api_styles/blob/main/01_libcurl_blocking_easy/file2.txt),
`CURL_get("localhost:5001/file1.txt")` and `CURL_get("localhost:5001/file2.txt")`
should work and return the content of the files, see [blocking libcurl section](#libcurl_easy).

## libcurl, asynchronous API with multi interface {#libcurl_multi}

# blocking, synchronous (App_Blocking) {#sync}

## on error handling {#error_handling}

### assume success always (tooling) {.unnumbered .unlisted}
### implicit, return empty string {.unnumbered .unlisted}
### status code, out parameter (std::filesystem-style) {.unnumbered .unlisted}
### optional {.unnumbered .unlisted}
### exceptions {.unnumbered .unlisted}
### result/variant-like {.unnumbered .unlisted}
### result/tuple-like {.unnumbered .unlisted}
### result/specialized {.unnumbered .unlisted}

# async polling, tasks  (App_Tasks)
# blocking std::future/promise
# async polling, std::future/promise
# async, callbacks (App_Callbacks)
# async, callbacks + polling (tasks, handle)
# async with statefull/implicit callback (state.on_X.subscribe/delegates)
# coroutines on top of callbacks (App_Coro)
# coroutines on top polling tasks
# fibers (WIN32) (App_Fibers)
# senders
# reactive streams

