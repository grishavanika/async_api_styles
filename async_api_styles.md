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

    [Work In Progress]{.mark}.

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

Next, lets have idiomatic C-style callbacks API, intentionally,
**not** C++ one, see [the note](#libcurl_c_style), to run requests concurrently:

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

doing 2 GET requests is more involved now:

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

# setup with cmake + libcurl {#cmake}

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

# building blocking API {#libcurl_easy}

Source code: [main.cc](https://github.com/grishavanika/async_api_styles/blob/main/01_libcurl_blocking_easy/main.cc).

Our blocking, synchronous API for GET request is straightforward,
lets go with function that looks like this:

``` cpp {.numberLines}
std::string CURL_get(const std::string& url);
```

libcurl comes with two different APIs,
["easy" and "multi"](https://curl.se/libcurl/c/). Lets use easy interface;
libcurl examples available online, including official [simple.c example](https://curl.se/libcurl/c/simple.html)
for a start.

Everything together leads to the implementation below, where
`curl_easy_perform()` call is the main one that blocks the execution
until request complete; once complete, we can return results:

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
"crash the whole application". `assert()` is enabled always **intentionally**
to simplify both, the sample code **and** debugging:

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

## run simple http server for tests {#serve}

To run sample code, lets use Python to have simple HTTP server that hosts
files in the current directory, see [serve.cmd](https://github.com/grishavanika/async_api_styles/blob/main/01_libcurl_blocking_easy/serve.cmd):

``` bash {.numberLines}
python -m http.server 5001
```

Given the directory that has [file1.txt](https://github.com/grishavanika/async_api_styles/blob/main/01_libcurl_blocking_easy/file1.txt)
and [file2.txt](https://github.com/grishavanika/async_api_styles/blob/main/01_libcurl_blocking_easy/file2.txt),
`CURL_get("localhost:5001/file1.txt")` and `CURL_get("localhost:5001/file2.txt")`
should work and return the content of the files, see [blocking libcurl section](#libcurl_easy).

# building classic C-style callbacks API {#libcurl_multi}

Source code: [main.cc](https://github.com/grishavanika/async_api_styles/blob/main/02_libcurl_callbacks_multi/main.cc).

## thoughts on the design {#libcurl_multi_design}

Now, lets imagine simplest possible asynchronous API. The difference to
[blocking API](#libcurl_easy) is that we ask the system to start a GET request
and the response should arrive some time later. The system invokes a
user-provided `callback` to notify us once everything is done:

``` cpp {.numberLines}
void CURL_async_get(const std::string& url, void (*callback)(std::string));
```

we could use it like this:

``` cpp {.numberLines}
// start a request:
CURL_async_get("localhost:5001/file2.txt"
    , [](std::string response)
{
    // probably, some time later:
    std::println("got response: {}", response);
});
```

There are multiple issues with the design above:

 1. Where is the "system" that starts the request? It could be implicit, hidden
    global singleton, but we can also ask a user to explicitly create and pass
    it around.
 2. The callback accepts only `response`, there is no way for a user to access
    other data, without resorting to global singletons again. When starting a 
    request, user should be able to provide opaque pointer to some data that
    system does not touch and simply gives it back in callback.
 3. When and from where the "system" invokes a `callback`? There are multiple
    answers, but we go with user-controlled event loop that drives the system.

To solve first issue, lets have explicit API to create and destroy the system:

``` cpp {.numberLines}
using CURL_Async = void*; // system's state

CURL_Async CURL_async_create();
void CURL_async_destroy(CURL_Async curl_async);
```

where `CURL_Async` is the system itself, since user does not care what's that
exactly, it's hidden under `void*`. User could create the system, use it and,
once not needed, destroy - to clean up resources, if any.

To drive a system with event loop, user must call the next API:

``` cpp {.numberLines}
void CURL_async_tick(CURL_Async curl_async);
```

This is the chance for a system to actually do some work over time **and**
invoke user-provided callbacks, if needed.

Lastly, to give a user some controll over data in the callback, we pass
opaque `void*` pointer around:

``` cpp {.numberLines}
// main async callback API
void CURL_async_get(CURL_Async curl_async
    , const std::string& url
    , void* user_data
    , void (*callback)(void* user_data, std::string response));
```

`user_data` could be anything, system gives it back when invoking `callback`.
This is user responsibility to ensure that pointer is valid all the time
while request is in progress.

Overall, everything included, we need to implement next API, see [below](#libcurl_multi_impl):

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

## note on C-style API (vs C++) {#libcurl_c_style}

For [C-style API above](#libcurl_multi_design), with C++, "the system"
could be a class, callback could be `std::function<>` to accept anything,
generally making it less verbose, having something like this:

``` cpp {.numberLines}
// the API:
class CURL_Async
{
public:
    void get(const std::string& url, std::function<void (std::string)>);
    void tick();
};

// the use:
CURL_Async curl;
curl.get("localhost:5001/file1.txt", [](std::string r)
{
    std::println("{}", r);
});
curl.tick(); // etc
```

However, C-style API we have is defacto standard, familiar
and reconized for asynchronous APIs with callbacks (citation needed).

The rest of asynchronous APIs implementations below are built on top of 
C-style callback API, as a basic building block to cover similar
callbacks-based APIs.

## implementing with libcurl multi {#libcurl_multi_impl}

Source code: [main.cc](https://github.com/grishavanika/async_api_styles/blob/main/02_libcurl_callbacks_multi/main.cc).

For implementation of [the API](#libcurl_multi_design):

``` cpp {.numberLines}
using CURL_Async = void*;
CURL_Async CURL_async_create();
void CURL_async_destroy(CURL_Async curl_async);
void CURL_async_tick(CURL_Async curl_async);
void CURL_async_get(CURL_Async curl_async
    , const std::string& url
    , void* user_data
    , void (*callback)(void* user_data, std::string response));
```

internally, lets have `CURL_AsyncScheduler` class to handle adding requests,
updating/ticking libcurl event loop and, in general, to represent
our whole `CURL_Async` system state:

``` cpp {.numberLines}
struct CURL_AsyncScheduler
{
    CURL_AsyncScheduler();
    ~CURL_AsyncScheduler();
    // no copy, no move
    CURL_AsyncScheduler(const CURL_AsyncScheduler&) = delete;

    using Callback = std::function<void (CURL* curl_easy)>;

    void tick();
    void add_request(CURL* curl_easy, Callback on_finish);

    // our state
    CURLM* _multi_curl = nullptr;
    std::unordered_map<CURL*, Callback> _curl_to_callback;
};
```

This is what we'll return to a user as `CURL_Async` pointer. Lets do it:

``` cpp {.numberLines}
CURL_Async CURL_async_create()
{
    CURL_AsyncScheduler* scheduler = new(std::nothrow) CURL_AsyncScheduler();
    assert(scheduler);
    return scheduler;
}

void CURL_async_destroy(CURL_Async curl_async)
{
    assert(curl_async);
    CURL_AsyncScheduler* scheduler = static_cast<CURL_AsyncScheduler*>(curl_async);
    delete scheduler;
}
```

Done. Now, user just needs to pass `CURL_Async` handle around.
Before implementing internals, lets have a helper function that gets actual
`CURL_AsyncScheduler` instance from opaque handle:

``` cpp {.numberLines}
CURL_AsyncScheduler& CURL_scheduler(CURL_Async curl_async)
{
    CURL_AsyncScheduler* scheduler = static_cast<CURL_AsyncScheduler*>(curl_async);
    assert(scheduler);
    return *scheduler;
}
```

It's not exposed to the user in any way. Lets implement our main API in terms
of our internal scheduler:

``` cpp {.numberLines}
void CURL_async_get(CURL_Async curl_async
    , const std::string& url
    , void* user_data
    , void (*callback)(void* user_data, std::string response))
{
    // 1. setup curl easy handle
    CURL* curl_easy = curl_easy_init();
    assert(curl_easy);
    CURLcode status = curl_easy_setopt(curl_easy, CURLOPT_URL, url.c_str());
    assert(status == CURLE_OK);
    status = curl_easy_setopt(curl_easy, CURLOPT_FOLLOWLOCATION, 1L);
    assert(status == CURLE_OK);
    
    // 2. write response data to separate std::string
    std::string* state = new std::string{};
    status = curl_easy_setopt(curl_easy, CURLOPT_WRITEFUNCTION, CURL_OnWriteCallback);
    assert(status == CURLE_OK);
    status = curl_easy_setopt(curl_easy, CURLOPT_WRITEDATA, state);
    assert(status == CURLE_OK);

    // 3. associate with multi handle/event loop
    CURL_scheduler(curl_async).add_request(curl_easy
        , [state, user_data, callback](CURL* curl_easy)
    {
        long response_code = -1;
        const CURLcode status = curl_easy_getinfo(curl_easy, CURLINFO_RESPONSE_CODE, &response_code);
        assert(status == CURLE_OK);
        assert(response_code == 200L);
        curl_easy_cleanup(curl_easy);
        std::string data = std::move(*state);
        delete state;
        callback(user_data, std::move(data));
    });
}
```

There are few moving parts:

 1. we create and setup curl easy handle in the same way as for blocking call;
 2. we allocate separate `std::string` to write the response data to with the
    same `CURL_OnWriteCallback` callback as in [blocking implementation](#libcurl_easy);
 3. finally, we associate the request with event loop/multi handle

It could be done another way around, eliminating the need for separate
`std::string` allocation and few more optimizations, mainly with the help of
[associating user data with curl easy handle/CURLOPT_PRIVATE](https://curl.se/libcurl/c/CURLOPT_PRIVATE.html).
However, it's good enough for illustrative purposes.

After creation of curl easy handle, we associate it with curl multi handle:

``` cpp {.numberLines}
CURL_AsyncScheduler::CURL_AsyncScheduler()
{
    const CURLcode status = curl_global_init(CURL_GLOBAL_ALL);
    assert(status == CURLE_OK);
    _multi_curl = curl_multi_init();
    assert(_multi_curl);
}

CURL_AsyncScheduler::~CURL_AsyncScheduler()
{
    const CURLMcode status = curl_multi_cleanup(_multi_curl);
    assert(status == CURLM_OK);
    curl_global_cleanup();
}

void CURL_AsyncScheduler::add_request(CURL* curl_easy, Callback on_finish)
{
    assert(on_finish);
    assert(curl_easy);
    assert(!_curl_to_callback.contains(curl_easy));

    const CURLMcode status = curl_multi_add_handle(_multi_curl, curl_easy);
    assert(status == CURLM_OK);
    _curl_to_callback[curl_easy] = std::move(on_finish);
}
```

`_curl_to_callback` map is used to be able to retrieve callback later, given
curl easy handle (`CURL*`).

Our user-exposed `CURL_async_tick()` API is implemented in terms of scheduler:

``` cpp {.numberLines}
void CURL_async_tick(CURL_Async curl_async)
{
    CURL_scheduler(curl_async).tick();
}

void CURL_AsyncScheduler::tick()
{
    int running_handles = -1;
    CURLMcode status = curl_multi_perform(_multi_curl, &running_handles);
    assert(status == CURLM_OK);
    int msgs_in_queue = 0;
    while (CURLMsg* m = curl_multi_info_read(_multi_curl, &msgs_in_queue))
    {
        if (m->msg != CURLMSG_DONE)
        {
            continue;
        }
        CURL* curl_easy = m->easy_handle;
        assert(curl_easy);
        status = curl_multi_remove_handle(_multi_curl, curl_easy);
        assert(status == CURLM_OK);
        auto it = _curl_to_callback.find(curl_easy);
        assert(it != _curl_to_callback.end());
        Callback callback = std::move(it->second);
        assert(callback);
        (void)_curl_to_callback.erase(it);
        callback(curl_easy);
    }
}
```

The main part of event loop is the call to `curl_multi_perform()`. Once done
we ask for easy handle requests that were completed, search for an
associated callback for each request and invoke it.

Note, there are no threads involved and it's possible to create many GET
requests at once with multiple calls to `CURL_async_get()` - libcurl will
manage them all together.

Again, it's user responsibility to drive libcurl with a periodic calls to 
`CURL_async_tick()`. Lets do single request with the API above ([source code](https://github.com/grishavanika/async_api_styles/blob/main/02_libcurl_callbacks_multi/main.cc)):

``` cpp {.numberLines}
#include <print>

int main()
{
    struct State
    {
        std::string response;
        bool done = false;
    };
    CURL_Async curl_async = CURL_async_create();
    State state;
    CURL_async_get(curl_async, "localhost:5001/file1.txt", &state
        , [](void* user_data, std::string response)
    {
        State& state = *static_cast<State*>(user_data);
        state.response = std::move(response);
        state.done = true;
    });
    while (!state.done)
    {
        CURL_async_tick(curl_async);
    }
    CURL_async_destroy(curl_async);

    std::println("async response: '{}'", state.response);
}
```

If [python HTTP server](#serve) is running, our program should print:

```
async response: 'content 1'

```

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

# building C++20 coroutines API

Coroutines materials:

 - [How C++ coroutines work](https://kirit.com/How%20C%2B%2B%20coroutines%20work).
 - All of [Asymmetric Transfer](https://lewissbaker.github.io/),
   author of [cppcoro](https://github.com/lewissbaker/cppcoro).

There are several moving and a bit unrelative parts to have working coroutines
code. First, coroutine function return type needs to be built, just to be able
to write any/empty coroutine:

``` cpp {.numberLines}
Co_Task coro_work()
{
    co_return;
}
```

Next, there is a need to write coroutine awaitable to be able to `co_await` some
work, specifically, GET request:

``` cpp {.numberLines}
Co_Task coro_work(CURL_Async curl_async)
{
    std::string response = co_await CURL_await_get(curl_async, "localhost:5001/file1.txt");
    co_return;
}
```

And, finally, there are some challenges to have a code that has several GET
requests on the fly with coroutines.

Lets start with basics.

## C++ coroutines, basic task type

Source code: [main.cc](https://github.com/grishavanika/async_api_styles/blob/main/0x_cpp_coro_task/main.cc).

There is a trick of writing some basic C++20 coroutines code - **listen to
compiler**. Lets see what it takes to make the next code "work":

``` cpp {.numberLines}
Co_Task coro_work()
{
    co_return;
}
```

`Co_Task` is a class, lets have empty one and try to compile:

``` cpp {.numberLines}
struct Co_Task {};

Co_Task coro_work()
{
    co_return;
}
```

MSVC complains:

```
main.cc(164,5): error C3774: cannot find 'std::coroutine_traits':
                Please include <coroutine> header
```

after including `<coroutine>` header:

```
main.cc(166,5): error C2039: 'promise_type': is not a member of
                'std::coroutine_traits<Co_Task>'
```

Lets add empty `promise_type` class inside `Co_Task`:

``` cpp {.numberLines}
#include <coroutine>

struct Co_Task
{
    struct promise_type {};
};

Co_Task coro_work()
{
    co_return;
}
```

MSVC complains:

```
main.cc(170,1): error C3789: this function cannot be a coroutine:
                'Co_Task::promise_type' does not declare the member
                'get_return_object()'
main.cc(170,1): error C3789: this function cannot be a coroutine:
                'Co_Task::promise_type' does not declare the member
                'initial_suspend()'
main.cc(170,1): error C3789: this function cannot be a coroutine:
                'Co_Task::promise_type' does not declare the member
                'final_suspend()'
```

Ah, so `promise_type` should have `get_return_object()`, `initial_suspend()`
and `final_suspend()` member functions. Return types are unclear, unfortunately.
To speed-up things, we know that `get_return_object()` should return `Co_Task`.
For `initial_suspend()` and `final_suspend()` we'll go with
`std::suspend_always` awaitables for now. That gives:

``` cpp {.numberLines}
#include <coroutine>

struct Co_Task
{
    struct promise_type
    {
        Co_Task get_return_object()           { return {}; }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend()   { return {}; }
    };
};

Co_Task coro_work()
{
    co_return;
}
```

MSVC complains:

```
main.cc(164,12): error C3781: Co_Task::promise_type: a coroutine's
                 promise must declare either
                 'return_value' or 'return_void'
main.cc(176,1): error C2039: 'unhandled_exception': is not a member
                of 'Co_Task::promise_type'
```

Since our `coro_work()` coroutine has just `co_return`, we should provide
`return_void()` member function. With `unhandled_exception()`, we have:

``` cpp {.numberLines}
struct promise_type
{
    Co_Task get_return_object()           { return {}; }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend()   { return {}; }
    void return_void()                    {}
    void unhandled_exception()            {}
};
```

MSVC complains:

```
main.cc(168,29): error C5231: the expression
                 'co_await promise.final_suspend()' must be non-throwing
```

Ok, makes sense. Finally,

``` cpp {.numberLines}
#include <coroutine>

struct Co_Task
{
    struct promise_type
    {
        Co_Task get_return_object()                  { return {}; }
        std::suspend_always initial_suspend()        { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void()                           {}
        void unhandled_exception()                   {}
    };
};

Co_Task coro_work()
{
    co_return;
}
```

compiles! We just need to fill in details and implement given functions
properly.

There are way too many different ways to implement coroutine task/promise types.
There are no constraints and, in general, it all depends on your design and
needs. We'll go with simplest working one for now:

 1. `Co_Task` will own coroutine handle (as in free coroutine in the
    destructor).
 2. Because of the above, `final_suspend()` must suspend always.
 3. Co_Task will be a "lazy" coroutine, meaning, it's going to be suspended
    after initial call of `coro_work()`/coroutine function.
 4. Because of the above, `initial_suspend()` must suspend.
 5. Because coroutine is suspended initially, `Co_Task` needs to expose
    `resume()` or similar function to run coroutine.

For now, lets proceed with implementation. Since we own coroutine, our 
`Co_Task` needs to have destructor, should be move-only:

``` cpp {.numberLines}
struct Co_Task
{
    struct promise_type;
    using co_handle = std::coroutine_handle<promise_type>;

    struct promise_type
    {
        Co_Task get_return_object()
        {
            return Co_Task{co_handle::from_promise(*this)};
        }
        // ...
    };

    Co_Task(co_handle coro)
        : _coro{coro} {}
    Co_Task(Co_Task&& rhs) noexcept
        : _coro{std::exchange(rhs._coro, {})} { }
    Co_Task(const Co_Task&) = delete;
    ~Co_Task() noexcept
    {
        if (_coro)
        {
            _coro.destroy();
        }
    }

    co_handle _coro;
};
```

In short, when we call `coro_work()`, compiler creates `Co_Task::promise_type`
and invokes `get_return_object()` to be able to return an instance of `Co_Task`
to the user. Here, in `get_return_object()` there is a way to get an access
to `std::coroutine_handle<>` - the only way to interact with just alocated
coroutine. Once `Co_Task` is created, we return it to the user.
It's **up to the user** to manage `Co_Task`. In our case, we own just created
coroutine, hence if `Co_Task` is destroyed, we assume coroutine is in suspended
state and destroy it too.

Writing down the rest of functions:

``` cpp {.numberLines}
std::suspend_always promise_type::initial_suspend()
{
    return {};
}

std::suspend_always promise_type::final_suspend() noexcept
{
    return {};
}

void promise_type::return_void()
{
    // yeah, we return void. Nothing to do
}

void promise_type::unhandled_exception()
{
    // crash, no exceptions handling
    assert(false);
}
```

we can test the basics:

``` cpp {.numberLines}
Co_Task coro_work()
{
    std::println("inside coro_work");
    co_return;
}

int main()
{
    Co_Task coro = coro_work(); 
}
```

which runs and... prints nothing since our coroutine is created and immediately
suspended even before executing first print.

Lets expose `resume()` for our `Co_Task` and use it:

``` cpp {.numberLines}
void Co_Task::resume()
{
    assert(_coro);
    assert(!_coro.done());
    _coro.resume();
}

Co_Task coro_work()
{
    std::println("inside coro_work");
    co_return;
}

int main()
{
    std::println("-- before coro_work()");
    Co_Task coro = coro_work();
    std::println("-- after coro_work()");
    coro.resume();
    std::println("-- after resume()");
}
```

which prints:

```
-- before coro_work()
-- after coro_work()
inside coro_work
-- after resume()
```

## C++ coroutines, basic await

Given that we can have simplest coroutine, what does it take to co_await?
Lets try to compile:

``` cpp {.numberLines}
struct Co_CurlAsync {};

Co_Task coro_work()
{
    co_await Co_CurlAsync{};
    co_return;
}
```

MSVC complains:

```
main.cc(73,26): error C2039: 'await_ready': is not a member of 'Co_CurlAsync'
main.cc(73,26): error C2039: 'await_suspend': is not a member of 'Co_CurlAsync'
main.cc(70,26): error C2039: 'await_resume': is not a member of 'Co_CurlAsync'
```

So `co_await` requires "awaiter" to have those 3 functions. We can think about
awaiter as something that:

 1. knows if some operation is ready
 2. knows how to suspend **and** (optionally) resume coroutine later
 3. knows how to get the result of awaited operation

The compiler asks awaiter, specifically, `Co_CurlAsync` with
`bool await_ready()` if operation is done/ready or is in progress. If awaiter
returns false, the compiler switches current coroutine state to "suspended"
and invokes awaiter's `await_suspend(std::coroutine_handle<> coro)`
customization point which allows to remember current coroutine `coro` handle
to call `.resume()` later, once operation is done. Once coroutine is resumed,
compiler asks for a value from last awaiter responsible for suspend.

In short, we can have `Co_CurlAsync` awaiter that tells that (1) operation is
not ready yet (2) on suspend, resumes coroutine immediately and (3) returns
nothing:

``` cpp {.numberLines}
struct Co_CurlAsync
{
    bool await_ready()
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> coro)
    {
        std::println("-- inside suspend, resuming immediately");
        coro.resume();
    }

    void await_resume()
    {
        std::println("-- resume");
    }
};

Co_Task coro_work()
{
    std::println("before co_await");
    co_await Co_CurlAsync{};
    std::println("after co_await");
    co_return;
}

int main()
{
    Co_Task coro = coro_work();
    coro.resume();
}
```

which prints:

```
before co_await
-- inside suspend, resuming immediately
-- resume
after co_await
```

Now, on suspend, we did nothing, but immediately resumed coroutine.
But we also could start an async operation and, on finish, resume the coroutine.

# coroutines on top polling tasks
# fibers (WIN32) (App_Fibers)
# senders
# reactive streams

