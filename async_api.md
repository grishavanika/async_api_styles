---
title: Asynchronous API
include-before: |

    Below, we showcase different variations of asynchronous APIs
    built on top of C-style callbacks with examples of
    doing 2 GET requests - both sequentially and concurrently.

    NO threads are involved intentionally, to disconnect any associations
    of coroutines or fibers with multithreading. Some parts are
    deliberately simple, while still presenting as much details as possible.

    Jump to examples for [blocking](#app_blocking), [callbacks](#app_callbacks),
    tasks, std::future, [coroutines](#app_coroutines),
    [fibers](#app_fibers), senders code.

    [Work In Progress]{.mark}.

---

--------------------------------------------------------------------------------

# introduction {#intro}

We start with a simple C-style API on top of
[libcurl C API](https://curl.se/libcurl/c/) and have a code that may
look like this:

``` cpp {.numberLines}
// our CURL API
std::string CURL_get(const std::string& url);

int main()
{
    const std::string r1 = CURL_get("localhost:5001/file1.txt");
    const std::string r2 = CURL_get("localhost:5001/file2.txt");
    std::println("{}", r1);
    std::println("{}", r2);
}
```

The code above performs two GET requests sequentially. Everything executes
synchronously.

Next, lets have a simple C-style callbacks API (**not** the C++ one, see
[the note](#libcurl_c_style)), to run requests concurrently:

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
    std::println("{}", state.r1);
    std::println("{}", state.r2);
}
```

There is a need to have a `State` for bookkeeping, pass it as a
`void*` user data to access later and, finally, run an event loop
to give libcurl a chance to process requests. Note, however,
requests execute concurrently now, as in - 2 requests are active at the
same time.

After this, lets build other variations of asynchronous API on top of C-style
callbacks above:

``` cpp {.numberLines}
std::string CURL_get(const std::string& url);

void CURL_async_get(CURL_Async curl_async
    , const std::string& url
    , void* user_data
    , void (*callback)(void* user_data, std::string response));

Co_CurlAsync CURL_await_get(
    CURL_Async curl_async, const std::string& url);
Co_Task<std::string> CURL_coro_get(
    CURL_Async curl_async, std::string url);

std::string CURL_fiber_get(
    CURL_Async curl_async, const std::string& url);
FiberTask<std::string> CURL_fiber_get(
      CURL_Async curl_async
    , const std::string& url
    , FiberTaskScheduler& fiber_scheduler);

std::future<std::string> CURL_future_get(
    CURL_Async curl_async, const std::string& url);
```

But before that, lets wrap [libcurl C API](https://curl.se/libcurl/c/)
for our needs.

--------------------------------------------------------------------------------

# setup with CMake + libcurl {#cmake}

CODE: CH00_cmake

For [vcpkg](https://github.com/microsoft/vcpkg), there is an extensive
[documentation](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started)
available. In short:

``` bash {.numberLines}
git clone https://github.com/microsoft/vcpkg
cd vcpkg
bootstrap-vcpkg.bat
```

Assuming vcpkg is installed at `K:\vcpkg`, we also set VCPKG_ROOT 
and add this path to `PATH` environment variable so build scripts
can use `VCPKG_ROOT` and `vcpkg` without a need to know the exact location.

``` bash {.numberLines}
set VCPKG_ROOT=K:\vcpkg
set PATH=%VCPKG_ROOT%;%PATH%
```

For the project (lets say in a `K:\async_api` folder), vcpkg
[manifest mode](https://learn.microsoft.com/vcpkg/consume/manifest-mode)
is used. Together with `curl` setup, all required steps are

``` bash {.numberLines}
cd K:\async_api
vcpkg new --application
vcpkg add port curl
```

Note that to find exact `curl` package name, `vcpkg search curl` was used which
prints:

> curl    8.13.0#1    A library for transferring data with URLs

CMakeLists.txt now looks like this:

``` cmake {.numberLines}
cmake_minimum_required(VERSION 3.24 FATAL_ERROR)
project(async_api LANGUAGES CXX)

add_executable(00_cmake_libcurl main.cc)
target_compile_features(00_cmake_libcurl PUBLIC cxx_std_23)
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

To test that everything compiles and links, save main.cc:

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
cd K:\async_api
cmake -S . -B __build ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build __build --config Debug
:: run a test
.\__build\CH00_cmake\Debug\CH00_cmake.exe
```

This assumes `cmake.exe` is in your `PATH`, see `build.cmd`.

# building blocking API {#libcurl_easy}

CODE: CH01_libcurl_easy

Blocking, synchronous API for a GET request is straightforward.
We go with a function that looks like this:

``` cpp {.numberLines}
std::string CURL_get(const std::string& url);
```

libcurl comes with two different APIs,
["easy" and "multi"](https://curl.se/libcurl/c/). Lets use an easy interface;
libcurl examples available online, including official
[simple.c example](https://curl.se/libcurl/c/simple.html) for a start.

Everything together leads to the implementation below, where
`curl_easy_perform()` call is the main one that blocks the execution
until request completes; once complete, we can return results:

``` cpp {.numberLines}
#include <string>

#include <curl/curl.h>

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

static size_t CURL_OnWriteCallback(
    void* ptr, size_t size, size_t nmemb, void* data)
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
    status = curl_easy_setopt(curl
        , CURLOPT_WRITEFUNCTION, CURL_OnWriteCallback);
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
`localhost:5001/file1.txt`. See the
[next section on how to make it happen](#serve).

Once done, we should see the sample file1.txt content in the console output:

```
CURL_get(file1.txt): 'content 1'

```

## run simple http server for tests {#serve}

CODE: CH01_libcurl_easy

To run sample code, lets use Python to have simple HTTP server that hosts
files in the current directory, see `serve.cmd`:

``` bash {.numberLines}
python -m http.server 5001
```

Given the directory that has file1.txt and file2.txt,
`CURL_get("localhost:5001/file1.txt")` should work and return the content
of the file, see [blocking libcurl section](#libcurl_easy).

# building C-style callbacks API {#libcurl_multi}

CODE: CH02_libcurl_multi

## thoughts on the design {#libcurl_multi_design}

Now, lets imagine simplest possible asynchronous API. The difference to
[blocking API](#libcurl_easy) is that we ask the system to start a GET request
and the response could arrive some time later. The system invokes a
user-provided `callback` to notify us once everything is done:

``` cpp {.numberLines}
void CURL_async_get(const std::string& url
    , void (*callback)(std::string));
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
    answers, but we go with user-controlled event loop that drives everything.

To solve first issue, lets have explicit API to create and destroy the system:

``` cpp {.numberLines}
using CURL_Async = void*; // system's state

CURL_Async CURL_async_create();
void CURL_async_destroy(CURL_Async curl_async);
```

where `CURL_Async` is the system itself, since user does not care what's that
exactly, it's hidden under `void*`. User creates the system, uses it and,
once not needed, destroys to clean up resources, if any.

To drive a system with event loop, user must call the next API:

``` cpp {.numberLines}
void CURL_async_tick(CURL_Async curl_async);
```

This is the chance for a system to actually do some work over time **and**
invoke user-provided callbacks, if needed.

Lastly, to give a user some control over data in the callback, we pass
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

There are more nuances, like how frequently/when `CURL_async_tick()` should be
invoked by a user; but all this is left out of the scope.

Overall, everything included, we need to implement the next API,
see [below](#libcurl_multi_impl):

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

However, C-style API we have, is a de-facto standard, familiar
and recognized for asynchronous APIs with callbacks (citation needed).

The rest of asynchronous APIs implementations below are built on top of 
C-style callback API, as a basic building block to cover similar
callbacks-based APIs.

## implementing with libcurl multi {#libcurl_multi_impl}

CODE: CH02_libcurl_multi

For our callbacks [API](#libcurl_multi_design):

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
    CURL_AsyncScheduler* scheduler =
        static_cast<CURL_AsyncScheduler*>(curl_async);
    delete scheduler;
}
```

Now, user just needs to pass `CURL_Async` handle around.
Before implementing internals, lets have a helper function that gets actual
`CURL_AsyncScheduler` instance from opaque handle:

``` cpp {.numberLines}
CURL_AsyncScheduler& CURL_scheduler(CURL_Async curl_async)
{
    CURL_AsyncScheduler* scheduler =
        static_cast<CURL_AsyncScheduler*>(curl_async);
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
    status = curl_easy_setopt(curl_easy
        , CURLOPT_WRITEFUNCTION, CURL_OnWriteCallback);
    assert(status == CURLE_OK);
    status = curl_easy_setopt(curl_easy, CURLOPT_WRITEDATA, state);
    assert(status == CURLE_OK);

    // 3. associate with multi handle/event loop
    CURL_scheduler(curl_async).add_request(curl_easy
        , [state, user_data, callback](CURL* curl_easy)
    {
        long response_code = -1;
        const CURLcode status = curl_easy_getinfo(curl_easy
            , CURLINFO_RESPONSE_CODE, &response_code);
        assert(status == CURLE_OK);
        assert(response_code == 200L);
        curl_easy_cleanup(curl_easy);
        std::string data = std::move(*state);
        delete state;
        callback(user_data, std::move(data));
    });
}
```

There are few moving parts and issues:

 1. we create and setup curl easy handle in the same way as for blocking call;
 2. we allocate separate `std::string` to write the response data to with the
    same `CURL_OnWriteCallback` callback as in the
    [blocking implementation](#libcurl_easy);
 3. finally, we associate the request with event loop/multi handle
 4. new `std::string` will leak the memory if the request is not completed
 5. overall, there are more hidden allocations from within `add_request()`:
    a) std::function<> most likely allocates
    b) std::unordered_map allocates

It could be done another way around, eliminating the need for separate
`std::string` allocation and few more optimizations, mainly with the help of
associating user data with curl easy handle/
[CURLOPT_PRIVATE](https://curl.se/libcurl/c/CURLOPT_PRIVATE.html).
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
`CURL_async_tick()`. Lets do single request with the API above:

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

``` cpp {.numberLines}
async response: 'content 1'
```

# blocking, synchronous (App_Blocking) {#sync .unlisted .unnumbered}

## on error handling {#error_handling .unlisted .unnumbered}

### assume success always (tooling) {.unlisted .unnumbered}
### implicit, return empty string {.unlisted .unnumbered}
### status code, out parameter (std::filesystem-style) {.unlisted .unnumbered}
### optional {.unlisted .unnumbered}
### exceptions {.unlisted .unnumbered}
### result/variant-like {.unlisted .unnumbered}
### result/tuple-like {.unlisted .unnumbered}
### result/specialized {.unlisted .unnumbered}

# async polling, tasks  (App_Tasks) {.unlisted .unnumbered}
# blocking std::future/promise {.unlisted .unnumbered}
# async polling, std::future/promise {.unlisted .unnumbered}
# async, callbacks (App_Callbacks) {.unlisted .unnumbered}
# async, callbacks + polling (tasks, handle) {.unlisted .unnumbered}
# async with statefull/implicit callback (state.on_X.subscribe/delegates) {.unlisted .unnumbered}

# building C++20 coroutines API

Coroutines materials:

 - [How C++ coroutines work](https://kirit.com/How%20C%2B%2B%20coroutines%20work).
 - All of [Asymmetric Transfer](https://lewissbaker.github.io/),
  author of [cppcoro](https://github.com/lewissbaker/cppcoro).

In short, we'd like to be able to write something like this:

``` cpp {.numberLines}
const std::string response = co_await CURL_await_get(
    curl_async, "localhost:5001/file1.txt");
// use `response` as a usual variable, no callbacks
```

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
    std::string response = co_await CURL_await_get(curl_async
        , "localhost:5001/file1.txt");
    co_return;
}
```

And, finally, there are some challenges to have a code that has several GET
requests on the fly, with coroutines.

Lets start with basics.

## C++ coroutines, basic task type

CODE: CH0x_coro_basic_task

There is a trick to writing some basic C++20 coroutines code - **listen to the
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

``` {.numberLines}
main.cc(164,5): error C3774: cannot find 'std::coroutine_traits':
                Please include <coroutine> header
```

after including `<coroutine>` header:

``` {.numberLines}
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

``` {.numberLines}
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

``` {.numberLines}
main.cc(164,12): error C3781: Co_Task::promise_type: a coroutine's
                 promise must declare either
                 'return_value' or 'return_void'
main.cc(176,1):  error C2039: 'unhandled_exception': is not a member
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

``` {.numberLines}
main.cc(168,29): error C5231: the expression
                 'co_await promise.final_suspend()' must be non-throwing
```

OK, makes sense. Finally,

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
needs. We'll go with an owning coroutine task type:

 1. `Co_Task` will own coroutine handle (as in free coroutine in the
    destructor).
 2. Because of the above, `final_suspend()` must suspend always.
 3. Co_Task will be a "lazy" coroutine, meaning, it's going to be suspended
    after initial call of `coro_work()`/coroutine function.
 4. Because of the above, `initial_suspend()` must suspend.
 5. Because coroutine is suspended initially, `Co_Task` needs to expose
    `resume()` or similar function to run a coroutine.

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
to `std::coroutine_handle<>` - the only way to interact with just allocated
coroutine. Once `Co_Task` is created, we return it to the user.
It's **up to the user** to manage `std::coroutine_handle<>`.
In our case, we own just created coroutine, hence if `Co_Task` is destroyed,
we assume coroutine is in suspended state and destroy it too.

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

CODE: CH0x_coro_basic_await

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

``` {.numberLines}
main.cc(73,26): error C2039: 'await_ready': is not a member of 'Co_CurlAsync'
main.cc(73,26): error C2039: 'await_suspend': is not a member of 'Co_CurlAsync'
main.cc(70,26): error C2039: 'await_resume': is not a member of 'Co_CurlAsync'
```

So `co_await` requires "awaiter" to have those 3 functions. We can think about
awaiter as something that:

 1. knows if some operation is ready or not (`..._ready`)
 2. knows how to resume coroutine later (`..._suspend`)
 3. knows how to get the result of awaited operation (`..._resume`)

The compiler asks awaiter, specifically, `Co_CurlAsync` with
`bool await_ready()` if operation is done/ready or is in progress. If awaiter
returns false, the compiler switches current coroutine state to "suspended"
and invokes awaiter's `await_suspend(std::coroutine_handle<> coro)`
customization point which allows to remember currently suspended coroutine
`coro` handle, to call `.resume()` later, once operation is done.
Once coroutine is resumed, compiler asks for a value from last awaiter
responsible for suspend.

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

## C++ coroutines, await callback with a crash

CODE: CH0x_coro_curl0

Lets continue implementing `Co_CurlAsync` above, in short:

``` cpp {.numberLines}
struct Co_CurlAsync
{
    CURL_Async _curl_async{};
    std::string _url;
    std::coroutine_handle<> _coro;
    std::string _response;

    bool await_ready()
    { // 1. CURL_async_get() is not yet started, force coroutine suspend:
        return false;
    }

    void await_suspend(std::coroutine_handle<> coro)
    { // 2. remember coroutine handle, start request, resume on finish:
        _coro = coro;

        CURL_async_get(_curl_async, _url, this
            , [](void* user_data, std::string response)
        {
            Co_CurlAsync& self = *static_cast<Co_CurlAsync*>(user_data);
            self._response = std::move(response);
            self._coro.resume();
        });
    }

    std::string await_resume()
    { // 3. after resume, return response:
        return std::move(_response);
    }
};

Co_CurlAsync CURL_await_get(CURL_Async curl_async, const std::string& url)
{
    return Co_CurlAsync{._curl_async = curl_async, ._url = url};
}
```

So, now `co_await CURL_await_get(..., "url")` should compile and kind-a work.
As always, there are few moving part.

When coroutine function (represented as `std::coroutine_handle<>`) `co_await`s
our CURL awaiter - Co_CurlAsync, we:

1. force whole coroutine to suspend, since we return false from `await_ready()`
2. this is needed so compiler invokes `await_suspend()` and gives us
   a handle to currently awaiting coroutine, so we can (a) start
   request and (b) resume coroutine with a call to `coro.resume()`
3. finally, once request is complete, we can return the `_response` from
   `await_resume()`

**There is one big issue there**: what if we start a request with
`CURL_async_get()`, coroutine suspends, BUT user discards `Co_Task` value
that destroys coroutine, making `std::coroutine_handle<>` we remembered -
dangling? There are several possible solutions, but lets see the current code
in action by writing our main() function:

``` cpp {.numberLines}
Co_Task coro_main(CURL_Async curl_async)
{
    const std::string response = co_await CURL_await_get(
        curl_async, "localhost:5001/file1.txt");

    std::println("coro_main response: '{}'", response);
    co_return;
}

int main()
{
    CURL_Async curl_async = CURL_async_create();
    Co_Task task = coro_main(curl_async);
    task.resume();
    while (task.is_in_progress())
    {
        CURL_async_tick(curl_async);
    }
    CURL_async_destroy(curl_async);
}
```

Here, we setup `CURL_Async`, as usual, and drive the loop until coroutine is
in progress:

``` cpp {.numberLines}
bool Co_Task::is_in_progress() const
{
    assert(_coro);
    return !_coro.done();
}
```

Running the sample should print:

```
coro_main response: 'content 1'

```

However, that works because we wait for coroutine until full complete. If
we discard Co_Task too early, there is going to be a crash:

``` cpp {.numberLines}
int main()
{
    CURL_Async curl_async = CURL_async_create();

    {
        Co_Task task = coro_main(curl_async);
        task.resume(); // run
    }   // **destroy**

    while (true)
    {
        CURL_async_tick(curl_async); // resume coroutine from there
    }
    CURL_async_destroy(curl_async);
}
```

It happens because `CURL_async_get()` callback remembers 2 pointers:

 1. `this` pointer to Co_CurlAsync/awaiter which is owned by coroutine frame
 2. and `coroutine_handle<>` itself, which we destroy BEFORE `CURL_async_get()`
    finish.

In short, we start request, then `.destroy()` coroutine, then try
to resume dangling coroutine inside a callback with a call to `.resume()`
even using stale pointer to awaiter (user data in the callback).

There are several solutions, few of them:

 1. Don't own and don't destroy coroutine inside Co_Task destructor
    (.. in a multiple ways).
 2. Delay coroutine destroy if there are live references to it.
 3. Be able to cancel `CURL_async_get()` request if coroutine/awaiter
    is destroyed.
 4. Ensure that callback has a safe way to detect dead coroutine and do nothing.

1st solution could be the best but changes completely the semantics of
`Co_Task`, does not allow to easily have `Co_Task<T>` that return some value
and requires to be able to change `Co_Task` internals.

2nd solution is similar in the sense that it also requires `Co_Task`
changes and the code around.

3rd solution requires changes to our basic C-style callback API which we assume
we can't do (since, otherwise, the interface is more advanced).

4th solution is the most inefficient and requires no changes neither in Co_Task
nor in callback API.

## C++ coroutines, await callback

CODE: CH0x_coro_curl

Lets fix the problematic part in a simple way:

``` cpp {.numberLines}
// struct Co_CurlAsync ...
void await_suspend(std::coroutine_handle<> coro)
{ // 2. remember coroutine handle, start request, resume on finish:
    _coro = coro;

    CURL_async_get(_curl_async, _url
        , this // ** HERE
        , [](void* user_data, std::string response)
    {
        Co_CurlAsync& self = *static_cast<Co_CurlAsync*>(user_data);
        self._response = std::move(response);
        self._coro.resume();
    });
}
```

For a "happy" path, when `CURL_async_get()` completes before coroutine
destruction, the flow is:

1) create Co_CurlAsync, invoke CURL_async_get
2) invoke callback (access this/coroutine)
3) destroy Co_CurlAsync

For a "bad" path, when coroutine is destroyed while we have CURL_async_get
in progress, the flow is:

1) create Co_CurlAsync, invoke CURL_async_get
2) destroy Co_CurlAsync
3) invoke callback (access this/dead coroutine)

Lets allocate a separate object that can outlive the coroutine/await.
There is an assumption that `CURL_async_get()` callback is always
going to be invoked. Given this, we:

A) allocate `WaitState` object - right before CURL_async_get()
B) pass it to the callback instead of `this`
C) destroy allocated object inside callback

This way we guarantee that the object is always alive while
request is in progress and its lifetime is bound the the request
itself and nothing else:

``` cpp {.numberLines}
_wait_state = new(std::nothrow) WaitState{._self = this};
assert(_wait_state);

CURL_async_get(_curl_async, _url
    , _wait_state
    , [](void* user_data, std::string response)
{
    WaitState* wait_state = static_cast<WaitState*>(user_data);
    assert(wait_state);
    if (wait_state->_self)
    {
        Co_CurlAsync& self = *wait_state->_self;
        self._wait_state = nullptr;
        self._response = std::move(response);
        self._coro.resume();
    }
    // else: Co_CurlAsync/coroutine is dead
    delete wait_state;
});
```

`WaitState` is just a struct that has a reference to `Co_CurlAsync`:

``` cpp {.numberLines}
struct Co_CurlAsync
{
    struct WaitState
    {
        Co_CurlAsync* _self = nullptr;
    };
    WaitState* _wait_state = nullptr;
```

When coroutine/Co_CurlAsync is destroyed, we need to mark `WaitState`
reference to it as null so `CURL_async_get()` knows it's not alive:

``` cpp {.numberLines}
~Co_CurlAsync()
{
    if (_wait_state)
    { // CURL_async_get() is still in progress
        assert(_wait_state->_self == this);
        _wait_state->_self = nullptr; // dead
    }
    // else: CURL_async_get() is already completed
}
```

That's how you write inefficient coroutine types for a systems
that know nothing about coroutines. When doing simple call:

``` cpp {.numberLines}
const std::string response = co_await CURL_await_get(
    curl_async, "localhost:5001/file1.txt");
```

we:

 1. allocate coroutine frame itself
 2. CURL_await_get allocates `WaitState`
 3. CURL_async_get allocates `std::string` to write a response
 4. CURL_async_get allocates `std::function` for a generic callback
 5. CURL_async_get allocates `std::unordered_map` node to remember
    what to call when
 6. .. and probably something else (CURL internals, etc)

"simple" CURL_async_get() implementation alone brings 3 allocations.
"simple" co_await CURL_async_get() brings 2 more separate allocations.

With CURL scheduler that **knows** about coroutines and few more
optimizations and limitations, this number of allocations can go down to amortized 0:

 * CURL scheduler preallocates up to N max requests
 * request itself knows how to store the response and coroutine-callback inline
 * coroutine itself re-uses memory pool for up to N max active coroutines.

Anyway, we now can co-await requests with a nice syntax:

``` cpp {.numberLines}
static Co_Task coro_main(CURL_Async curl_async)
{
    const std::string r1 = co_await CURL_await_get(
        curl_async, "localhost:5001/file1.txt");
    const std::string r2 = co_await CURL_await_get(
        curl_async, "localhost:5001/file2.txt");
    std::println("{}", r1);
    std::println("{}", r2);
    co_return;
}

int main()
{
    CURL_Async curl_async = CURL_async_create();
    Co_Task task = coro_main(curl_async);
    task.resume();
    while (task.is_in_progress())
    {
        CURL_async_tick(curl_async);
    }
    CURL_async_destroy(curl_async);
}
```

Note, however, we run 2 requests one after another: second request starts
when first co_await ends. There is no concurrency..

## coroutine task that holds a return value

CODE: CH0x_coro_task

To be more useful, we'd like to be able return something out of coroutine:

``` cpp {.numberLines}
static Co_Task<int> coro_main()
{
    co_return 3;
}

int main()
{
    Co_Task<int> task = coro_main();
    task.resume();
    std::println("coro main: {}", task.get_once());
}
```

It requires implementing promise_type `return_value()` and `return_void()`
customization points. The only complication is that we need to handle
`Co_Task<void>`. To do so, lets start with `promise_return<T>`:

``` cpp {.numberLines}
template<typename T>
struct promise_return
{
    std::variant<std::monostate, T> _value;
    template<typename U>
    void return_value(U&& v) noexcept
    {
        _value.template emplace<1>(std::forward<U>(v));
    }
    const T& get() const noexcept
    {
        assert(_value.index() == 1);
        return std::get<1>(_value);
    }
    T&& get_once() noexcept
    {
        const T& v = static_cast<const promise_return&>(*this).get();
        return std::move(const_cast<T&>(v));
    }
};

template<>
struct promise_return<void>
{
    void return_void() noexcept
    {
    }
    void get() const noexcept
    {
    }
    void get_once() noexcept
    {
    }
};

template<typename T>
struct promise_return<T&>
{
    static_assert(sizeof(T) == 0, "we do not support Co_Task<T&>");
};
```

promise_return for void injects `return_void()`, get() returns nothing.
promise_return for any type T injects `return_value()` and remembers the value.
std::variant is used to handle non-default-constructible types and, in general,
construct a value only at the point of the actual return/co_return.

With the help of promise_return, promise_type remains the same as our initial
version:

``` cpp {.numberLines}
template<typename T>
struct Co_Task
{
    struct promise_type;
    using co_handle = std::coroutine_handle<promise_type>;

    struct promise_type : promise_return<T>
    {
        Co_Task get_return_object() noexcept
        {
            return Co_Task{co_handle::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }
        std::suspend_always final_suspend() noexcept
        {
            return {};
        }
        void unhandled_exception() noexcept
        {
            // crash, no exceptions handling
            assert(false);
        }
    };

    // ...
    decltype(auto) get() const
    {
        assert(_coro);
        return _coro.promise().get();
    }
    decltype(auto) get_once() const
    {
        assert(_coro);
        return _coro.promise().get_once();
    }

    co_handle _coro;
};
```

Now `Co_Task<T>` works:

``` cpp {.numberLines}
static Co_Task<int> coro_main()
{
    co_return 3;
}
```

## awaiting coroutine task

CODE: CH0x_coro_await_task

Now, given `Co_Task<T>`, we need to await for it somehow:

``` cpp {.numberLines}
static Co_Task<int> coro_get()
{
    co_return 3;
}

static Co_Task<int> coro_main()
{
    const int v = co_await coro_get();
    co_return v;
}
```

There are 2 important implementation bits to know:

 - [Symmetric Transfer](https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer)
 - final_suspend() can return **custom** awaitable

`final_suspend()` allows to inject custom code for the moment of coroutine END, so
we can do something at that point in time:

``` cpp {.numberLines}
struct promise_type : promise_return<T>
{
    auto final_suspend() noexcept
    {
        struct Final_Await : std::suspend_always
        {
            void await_suspend(co_handle self_coro) noexcept
            {
                // **HERE**
            }
        };
        return Final_Await{};
    }
```

Given a simple coroutine:

``` cpp {.numberLines}
static Co_Task<int> coro_get()
{
    co_return 3;
} // <-- final_suspend()
```

our Final_Await, await_suspend() gets invoked after co_return,
passing a coroutine that is about to end. This is exactly what we need to
notify some other waiting code that we are done. We could invoke a callback... BUT
with a symmetric transfer, `await_suspend()` can return a **next** coroutine that
must be executed:

``` cpp {.numberLines}
struct promise_type : promise_return<T>
{
    std::coroutine_handle<> _waiting_coro = std::noop_coroutine();

    auto final_suspend() noexcept
    {
        struct Final_Await : std::suspend_always
        {
            std::coroutine_handle<> await_suspend(co_handle self_coro) noexcept
            {
                return self_coro.promise()._waiting_coro;
            }
        };
        return Final_Await{};
    }
```

Here, if no one waits for us, we return `std::noop_coroutine()` that does nothing.
Otherwise, if `_waiting_coro` was set, we end the execution of our coroutine
and resume any other coroutine that was waiting for us. To setup `_waiting_coro`,
we implement awaitable interface for Co_Task itself:

``` cpp {.numberLines}
template<typename T>
struct Co_Task
{
    bool await_ready()
    {
        assert(is_in_progress());
        return false;
    }
    // intentionally auto, not decltype(auto)
    auto await_resume()
    {
        return get_once();
    }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> waiting_coro)
    {
        _coro.promise()._waiting_coro = waiting_coro;
        return _coro; // resume us
    }

    co_handle _coro;
};
```

It's important to understand what is `_coro` and what is `waiting_coro`.
We have 3 moving parts (1) Final_Await::await_suspend() with self_coro,
(2) Co_Task::await_suspend() waiting_coro and (3) Co_Task `_coro` member itself:

``` cpp {.numberLines}
struct Co_Task
{
    [1] ... promise_type::Final_Await::await_suspend(co_handle self_coro) noexcept
    {
        return self_coro.promise()._waiting_coro;
    }
    [2] ... await_suspend(std::coroutine_handle<> waiting_coro)
    {
        _coro.promise()._waiting_coro = waiting_coro;
        return _coro; // resume us
    }
    [3] co_handle _coro;
};
```

Given our awaiting code sample:

``` cpp {.numberLines}
static Co_Task<int> coro_get()
{
    co_return 3;
}

static Co_Task<int> coro_main()
{
    co_return co_await coro_get();
}
```

We have 2 coroutines created, where:

1. `_coro` from part [3] is `coro_get()`
2. `waiting_coro` from part [2] await_suspend() is our `coro_main()`; and
3. `self_coro` from part [1] is `coro_get()` again that gets finished.

So, we

1. create coro_main()
2. create coro_get()
3. then await_suspend() on coro_get() from within coro_main()
4. that remembers that `_waiting_coro` is coro_main()
5. resumes coro_get() execution (by returning it, symmetric transfer); and then
5. we finish coro_get() and its final_suspend() resumes `_waiting_coro`
   which is coro_main().

That way coro_get() was scheduled to be executed because we co_await it from
within coro_main() and coro_main() was executed 2nd time because coro_get() was
completed.

One note on the co_await semantic and ownership: see how for await_resume()
we use `get_once()` to move the value out awaited task. In general, when
co_awaiting, we consume the task and it can't be used after co_await. So the next
code should be disallowed:

``` cpp {.numberLines}
static Co_Task<int> coro_main()
{
    Co_Task<int> t = coro_get(1);
    co_await t; // t is lvalue
    t.get(); // error
}
``` 

To enforce that, we use `operator co_await() &&` with `&&` ref-qualifier; see also
[C++ Coroutines: Understanding operator co_await](https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await):

``` cpp {.numberLines}
struct Co_Await
{
    Co_Task<T> _task;

    bool await_ready()
    {
        assert(_task.is_in_progress());
        return false;
    }
    // intentionally auto, not decltype(auto)
    auto await_resume()
    {
        return _task.get_once();
    }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> waiting_coro)
    {
        _task._coro.promise()._waiting_coro = waiting_coro;
        return _task._coro; // resume us
    }
};

Co_Await operator co_await() &&
{
    // consume this.
    return Co_Await{._task{std::move(*this)}};
}
```

Now we can co_await our tasks multiple times:

``` cpp {.numberLines}
static Co_Task<int> coro_get(int v)
{
    co_return v;
}

static Co_Task<int> coro_main()
{
    const int v1 = co_await coro_get(2);
    const int v2 = co_await coro_get(3);
    co_return (v1 + v2);
}

int main()
{
    Co_Task<int> task = coro_main();
    task.resume();
    std::println("coro main: {}", task.get_once());
}
```

Note, how we still execute `coro_get(2)` first then `coro_get(3)` second. There
is still no way to launch those 2 coroutines concurrently and wait for both of them:

``` cpp {.numberLines}
static Co_Task<int> coro_main()
{
    auto [v1, v2] = co_await CO_await_all(coro_get(2), coro_get(3));
    co_return (v1 + v2);
}
```

## waiting for multiple coroutines

Lets implement awaiting for multiple Co_Tasks:

``` cpp {.numberLines}
static Co_Task<int> coro_main()
{
    auto [v1, v2] = co_await CO_await_all(coro_get(2), coro_get(3));
    co_return (v1 + v2);
}
```

Note how we return a tuple of all awaited results (since our coroutine task
can never fail). Also, note how this is different to sequential co_await since
all children coroutine tasks are started at the point of await and executed
concurrently:

``` cpp {.numberLines}
static Co_Task<void> coro_main()
{
    int v1 = co_await coro_get(2);
    int v2 = co_await coro_get(3); // runs strictly AFTER first task
}
```

To await N tasks, our parent coroutine/task needs to be resumed only when last
of that N tasks completes. To achieve this, we introduce a counter that signals
how many tasks are still in progress. So our Final_Await can resume the parent
only when the wait count is zero. See the old implementation:

``` cpp {.numberLines}
struct promise_type : promise_return<T>
{
    std::coroutine_handle<> _waiting_coro = std::noop_coroutine();

    auto final_suspend() noexcept
    {
        struct Final_Await : std::suspend_always
        {
            std::coroutine_handle<> await_suspend(co_handle self_coro) noexcept
            {
                return self_coro.promise()._waiting_coro;
            }
        };
        return Final_Await{};
    }
```

and compare to a new one:

``` cpp {.numberLines}
struct promise_type : promise_return<T>
{
    std::int32_t* _wait_count = nullptr;
    co_handle _waiting_coro;

    std::coroutine_handle<> Final_Await::await_suspend(co_handle self_coro) noexcept
    {
        promise_type& self = self_coro.promise();
        if (self._waiting_coro)
        {
            assert(self._wait_count);
            std::int32_t& wait_count = *self._wait_count;
            wait_count -= 1;
            assert(wait_count >= 0);
            if (wait_count == 0)
            {
                return self._waiting_coro;
            }
            // else: we are not the last task, do nothing.
        }
        // else: no one was awaiting us.
        return std::noop_coroutine();
    }
```

When Co_Task ends, we:

1. check if someone waits for us
2. decrement wait counter by 1
3. see if we are the last one and resume awaiting coroutine

With this change, awaiting a single Co_Task requires setting the counter:

``` cpp {.numberLines}
struct Co_Await
{
    Co_Task<T> _task;
    std::int32_t _wait_count = 1;
    std::coroutine_handle<> Co_Await::await_suspend(
        std::coroutine_handle<> waiting_coro) noexcept
    {
        promise_type& promise = _task._coro.promise();
        promise._wait_count = &_wait_count; // 1
        promise._waiting_coro = waiting_coro;
        return _task._coro; // resume us
    }
};
```

For awaiting of N coroutines/tasks, we start with `CO_await_all()`:

``` cpp {.numberLines}
template<typename... Ts>
static auto CO_await_all(Co_Task<Ts>&&... tasks)
{
    static_assert(sizeof...(Ts) > 0);
    using Is = std::index_sequence_for<Ts...>;
    return Co_Await_All<Is, Ts...>{._tasks{std::move(tasks)...}};
}
```

It accepts variadic number of any tasks, consumes all of them and returns an awaiter
- Co_Await_All. We store all of the Co_Tasks and when wait completes, return a
tuple of all of the results:

``` cpp {.numberLines}
template<auto... Is, typename... Ts>
struct Co_Await_All<std::index_sequence<Is...>, Ts...>
{
    std::tuple<Co_Task<Ts>...> _tasks;
    std::int32_t _wait_count = sizeof...(Ts);
    bool await_ready()
    {
        // assert(_tasks[I].is_in_progress()...);
        return false;
    }
    auto await_resume()
    {
        return std::tuple<Ts...>{std::get<Is>(_tasks).get_once()...};
    }
};
```

When awaiting starts, we (a) setup wait_count=N and (b) resume all of the tasks,
also linking awaiting coroutine - as in the regular single-task await:

``` cpp {.numberLines}
void Co_Await_All::await_suspend(std::coroutine_handle<> waiting_coro)
{
    auto handle = [&](auto& Task)
    {
        auto& promise = Task._coro.promise();
        promise._wait_count = &_wait_count;
        promise._waiting_coro = waiting_coro;
        Task._coro.resume();
    };

    (handle(std::get<Is>(_tasks)), ...);
}
```

Finally, we can await several concurrently running Co_Tasks:

``` cpp {.numberLines}
static Co_Task<int> coro_get(int v)
{
    co_return v;
}

static Co_Task<int> coro_main()
{
    auto [v1, v2] = co_await CO_await_all(coro_get(2), coro_get(3));
    co_return (v1 + v2);
}

int main()
{
    Co_Task<int> task = coro_main();
    task.resume();
    std::println("{}", task.get_once());
}
```

## waiting for multiple CURL requests with tasks

CODE: CH0x_coro_await_curl

We already built CURL_await_get() that awaits CURL request within a coroutine.
With CO_await_all() API, we can await for multiple concurrent CURL requests
by wrapping every request into a separate coroutine task:

``` cpp {.numberLines}
Co_Task<std::string> CURL_coro_get(CURL_Async curl_async, std::string url)
{
    co_return co_await CURL_await_get(curl_async, url);
}

Co_Task<void> coro_main(CURL_Async curl_async)
{
    auto [r1, r2] = co_await CO_await_all(
        CURL_coro_get(curl_async, "localhost:5001/file1.txt"),
        CURL_coro_get(curl_async, "localhost:5001/file2.txt"),
        );
    std::println("{}", r1);
    std::println("{}", r2);
}
```

That's.. all. Note, however, 3 separate coroutines are allocated (one for coro_main
and two for CURL_coro_get). We can do less generic coroutine awaiter that skips
intermediate coroutines.

## waiting for multiple CURL request with custom awaitable

CODE: CH0x_coro_await_many

Instead of CURL_coro_get() + CO_await_all():

``` cpp {.numberLines}
Co_Task<std::string> CURL_coro_get(CURL_Async curl_async, std::string url)
{
    co_return co_await CURL_await_get(curl_async, url);
}
// ...
auto [r1, r2] = co_await CO_await_all(
    CURL_coro_get(curl_async, "localhost:5001/file1.txt"),
    CURL_coro_get(curl_async, "localhost:5001/file2.txt"),
    );
```

we can make a separate, specialized awaiter, like CURL_await_get_many():

``` cpp {.numberLines}
auto [r1, r2] = co_await CURL_await_get_many(curl_async,
    "localhost:5001/file1.txt",
    "localhost:5001/file2.txt"
    );
```

that skips intermediate CURL_coro_get() coroutine.

CURL_await_get_many() is almost the same as CURL_await_get() implementation:

``` cpp {.numberLines}
template<typename... URLs>
    requires std::conjunction_v<
        std::is_constructible<std::string, URLs&&>...>
auto CURL_await_get_many(CURL_Async curl_async, URLs... urls)
{
    Co_CurlAsyncMany<std::index_sequence_for<URLs...>> awaiter;
    awaiter._curl_async = curl_async;
    awaiter.setup_urls(std::move(urls)...);
    return awaiter;
}
```

Ignoring templates (to be able to accept variadic number of URLs), we create
Co_CurlAsyncMany awaitable that knows how to wait for N requests:

``` cpp {.numberLines}
template<typename Is>
struct Co_CurlAsyncMany;

template<auto... Is>
struct Co_CurlAsyncMany<std::index_sequence<Is...>>
{
    struct WaitState
    {
        std::int32_t _active_count = 0;
        Co_CurlAsyncMany* _self = nullptr;
    };

    static constexpr std::size_t Count = sizeof...(Is);
    CURL_Async _curl_async{};
    std::coroutine_handle<> _coro;

    WaitState* _wait_state = nullptr;
    std::array<std::string, Count> _urls;
    std::array<std::string, Count> _responses;

    template<typename... URLs>
    void setup_urls(URLs... urls)
    {
        ((_urls[Is] = std::move(urls)), ...);
    }

    // ...
};
```

Here, we remember all of URLs and have an `std::array<std::string, Count>` for
all of responses. Our WaitState passed to CURL_async_get() callback
also has a counter for active requests since we want
to complete only when last request is finished:

``` cpp {.numberLines}
bool Co_CurlAsyncMany::await_ready() noexcept
{
    return false;
}

void Co_CurlAsyncMany::await_suspend(std::coroutine_handle<> coro) noexcept
{
    _coro = coro;
    _wait_state = new(std::nothrow) WaitState;
    assert(_wait_state);
    _wait_state->_active_count = Count;
    _wait_state->_self = this;

    auto handle = [&]<auto I>(std::integral_constant<std::size_t, I>)
    {
        CURL_async_get(_curl_async, _urls[I]
            , _wait_state
            , [](void* user_data, std::string response)
        {
            WaitState* wait_state = static_cast<WaitState*>(user_data);
            assert(wait_state);
            if (on_response<I>(*wait_state, std::move(response)))
            {
                delete wait_state;
            }
        });
    };

    (handle(std::integral_constant<std::size_t, Is>{}), ...);
}

Co_CurlAsyncMany::~Co_CurlAsyncMany() noexcept
{
    if (_wait_state)
    {
        assert(_wait_state->_self == this);
        _wait_state->_self = nullptr; // dead
    }
}

std::array<std::string, Count> Co_CurlAsyncMany::await_resume() noexcept
{
    return std::move(_responses);
}
```

Basically, we:

1. start N requests; and
2. clean-up `wait_state` only for a last response
3. wait_state logic/lifetime handling is the same as is for Co_CurlAsync

Finally, `on_response<I>()` is also largely the same as Co_CurlAsync:
(a) we decrement active requests count, (b) see if coroutine is still alive,
(c) write a response to proper place and (c) resume awaiting coroutine
if we are the last response:

``` cpp {.numberLines}
template<auto I>
static bool Co_CurlAsyncMany::on_response(
    WaitState& wait_state, std::string&& response)
{
    wait_state._active_count -= 1;
    assert(wait_state._active_count >= 0);

    if (wait_state._self)
    {
        Co_CurlAsyncMany& self = *wait_state._self;
        self._responses[I] = std::move(response);
        if (wait_state._active_count == 0)
        {
            self._wait_state = nullptr;
            self._coro.resume();
        }
    }
    // else: Co_CurlAsyncMany/coroutine is dead

    if (wait_state._active_count == 0)
    {
        return true; // done
    }
    return false;
}
```

With that in mind, we can do:

``` cpp {.numberLines}
Co_Task<void> coro_main(CURL_Async curl_async)
{
    auto [r1, r2] = co_await CURL_await_get_many(curl_async,
        "localhost:5001/file1.txt",
        "localhost:5001/file2.txt"
        );
    std::println("{}", r1);
    std::println("{}", r2);
}
```

which is slightly different to a more generic version:

``` cpp {.numberLines}
Co_Task<void> coro_main(CURL_Async curl_async)
{
    auto [r1, r2] = co_await CO_await_all(
        CURL_coro_get(curl_async, "localhost:5001/file1.txt"),
        CURL_coro_get(curl_async, "localhost:5001/file2.txt"),
        );
    std::println("{}", r1);
    std::println("{}", r2);
}
```

For the rest of the sample code, we'll go using `CO_await_all()` version.

# coroutines on top polling tasks {.unlisted .unnumbered}

# building Fibers API

CODE: CH0x_fiber_basic

(Win32) Fibers materials:

 - [Using Fibers](https://learn.microsoft.com/en-us/windows/win32/procthread/using-fibers)
 - [Fibers: the Most Elegant Windows API](https://nullprogram.com/blog/2019/03/28/)

In short, we'd like to be able to write something like this:

``` cpp {.numberLines}
const std::string response = FF_await_get(
    curl_async, "localhost:5001/file1.txt");
// use `response` as a usual variable, no callbacks
```

to make an asynchronous request. No callbacks, no special keywords.

While "Using Fibers" example above is nice, it's still overly complicated to get
basic idea in a simpler form.

Going with Win32 Fibers, short intro is:

 - fibers allow to suspend and resume execution at any given point
   inside a function
 - they are stackful coroutines, as opposed to C++20 coroutines
   that are stackless
 - they implement symmetric coroutines (same as C++20 coroutines);
   we'll build asymmetric coroutines on top of Fibers
 - it should be trivial to ifdef POSIX implementation
   using deprecated `ucontext.h`

and the general idea is:

 - you create a fiber with `::CreateFiber()` API; it's suspended
 - you switch to/activate/run a fiber with `::SwitchToFiber()` call
 - you can switch only between fibers; so everything must be a fiber

Last point is more specific to Win32 API:
 
 - from within a `main()` entry point we are in a thread context
 - once `::CreateFiber()` gets you a fiber to switch to, (main) thread
   needs to be converted to a fiber with a call to `::ConvertThreadToFiber()`

But, ignoring thread-to-fiber conversion, we just (a) create N fibers
and (b) switch an execution between them. Worth mentioning: fibers still execute
withing a thread context, meaning - context switches (between threads)
still happen while fiber is executing.

Below, we go with a `Fiber` class that encapsulates all the system APIs above.
We'd like to have a working code that may look like this:

``` cpp {.numberLines}
void Fiber::run()
{
    std::println("fiber1");
    suspend();
    std::println("fiber2");
}

int main()
{
    Fiber fiber;
    std::println("main1");
    fiber.resume();
    std::println("main2");
    fiber.resume();
    std::println("main3");
}
```

and prints:

``` {.numberLines}
main1
fiber1
main2
fiber2
main3
```

## basic Fiber

CODE: CH0x_fiber_basic

We start with a `Fiber` class that allocates a fiber:

``` cpp {.numberLines}
struct Fiber
{
    void* _fiber = nullptr;
    void* _parent_fiber = nullptr;

    Fiber(const Fiber&) = delete;
    Fiber()
    {
        _fiber = ::CreateFiber(
            0 // default stack size
            , &FiberProc
            , this); // parameter to FiberProc
        assert(_fiber);
    }
    ~Fiber()
    {
        ::DeleteFiber(_fiber);
        _fiber = nullptr;
    }
```

To make our lives easier, there are few assumptions and simplifications:

 - we are on x64 system, so there is no need to use extended Fibers API
 - we assume `LPVOID` is `void*` so no Win32 API types are used
   (great for headers)
 - and, given x64, `WINAPI`/`__stdcall` can be omitted, so callbacks passed to
   Win32 API can have simple C++ declarations

That gives us next include:

``` cpp {.numberLines}
#include <Windows.h>
// Note: the floating-point state on x86 systems is not preserved.
// If there is need to support x86, Fiber's Ex-tended API must be used.
#if !defined(_WIN64)
#  error Fiber implementation does not support x86 systems.
#endif
#include <type_traits>
static_assert(std::is_same_v<LPVOID, void*>);
```

Next, while Win32 Fibers can switch from any one Fiber to any other Fiber,
we simplify and implement a Fiber that can be resumed, but when suspends -
goes to the point of last resume (its parent). Hence, `void* _parent_fiber`.

`FiberProc()` we pass to `::CreateFiber()` gets a reference to a given
`Fiber` instance and runs it. FiberProc must never end, hence a while loop
and a suspend if a call to run() ends:

``` cpp {.numberLines}
static void FiberProc(void* parameter)
{
    assert(parameter);
    Fiber& self = *static_cast<Fiber*>(parameter);
    while (true)
    {
        self.run();
        self.suspend();
    }
}
```

To suspend a Fiber is to switch to our parent fiber who did resume us:

``` cpp {.numberLines}
void suspend()
{
    assert(_parent_fiber);
    void* switch_to_fiber = _parent_fiber;
    _parent_fiber = nullptr;
    ::SwitchToFiber(switch_to_fiber);
}
```

To resume a Fiber, we simply call `::SwitchToFiber()` for a `_fiber` we created.
However, since when suspending, we need to know how to switch back, we remember
current fiber as our parent:

``` cpp {.numberLines}
void resume()
{
    assert(_parent_fiber == nullptr);
    _parent_fiber = ::GetCurrentFiber();
    ::SwitchToFiber(_fiber);
}
```

Remember, `.resume()` is, basically, a first call to a Fiber from within
a main function/thread. That means that `::GetCurrentFiber()` is invoked
in a context of `main()`:

``` cpp {.numberLines}
int main()
{
    Fiber fiber;
    fiber.resume(); // call to ::GetCurrentFiber()??
```

To make that work, specifically for Win32 API, main thread needs to become
a Fiber. This is what we do by having a simple RAII class:

``` cpp {.numberLines}
struct Fiber::Boot
{
    Boot(const Boot&) = delete;
    Boot()
    {
        const void* fiber = ::ConvertThreadToFiber(nullptr);
        assert(fiber);
    }
    ~Boot()
    {
        const auto ok = ::ConvertFiberToThread();
        assert(ok);
    }
};
```

Hence, main and/or any other thread that may use Fibers, needs to
scope `Fiber::Boot` variable on top:

``` cpp {.numberLines}
int main()
{
    Fiber::Boot _;

    Fiber fiber;
    std::println("main1");
    fiber.resume();
    std::println("main2");
    fiber.resume();
    std::println("main3");
}
```

It's possible to avoid that by calling `::ConvertThreadToFiber()`
on each and every call to `Fiber::resume()`; choose what you like more.

Given a main() above, we:

 - create a fiber, which is suspended initially
 - print "main1"
 - first call to `.resume()` switches us back to `FiberProc`
   that invokes `Fiber::run()`:

``` cpp {.numberLines}
void Fiber::run()
{
    std::println("fiber1");
    suspend();
    std::println("fiber2");
}
```

that:

 - prints "fiber1"
 - suspends itself, which switches back to main (our parent fiber)
 - main prints "main2", resumes fiber
 - fiber prints "fiber2", exits run(), but immediately suspends itself
 - main prints "main3"

All at once:

```
main1
fiber1
main2
fiber2
main3
```

## switching between Fibers

CODE: CH0x_fiber_switch

Section above shows how we can switch from a main to a different Fiber.
Fiber on it's own, when suspended, switches back to its invoker/resumer.
However, instead of suspend, Fiber can switch execution to a different Fiber.
While not quite used anywhere else, lets show the possibility.
Our `Fiber::run()` must be able to run different code; lets inject any
user-defined callback and run it:

``` cpp {.numberLines}
struct Fiber
{
    // For an example:
    std::function<void ()> _callback;
    // ...
};

void Fiber::run()
{
    assert(_callback);
    _callback();
}
```

Where main can now drive 2 Fibers at once:

``` cpp {.numberLines}
void Fiber::run()
{
    assert(_callback);
    _callback();
}

int main()
{
    Fiber::Boot _;

    Fiber fiber1;
    Fiber fiber2;
    fiber1._callback = [&fiber2, self = &fiber1]()
    {
        std::println("fiber1: start");
        fiber2.resume(); // switch to fiber2
        std::println("fiber1: END");
        self->suspend(); // switch to main (=our parent)
    };
    fiber2._callback = [self = &fiber2]()
    {
        std::println("fiber2: start");
        self->suspend(); // switch back to fiber1 (=our parent)
        std::println("fiber2: END");
        self->suspend(); // switch to main (=our parent)
    };
    std::println("main1");
    fiber1.resume();
    std::println("main2");
    fiber2.resume();
    std::println("main3");
}
```

we:

 - create 2 fibers; they are suspended
 - print "main1"
 - resume fiber1, that starts run(), that prints "fiber1: start"
 - instead of suspend/switch to main, we then resume/switch to fiber2
 - fiber2 resume prints "fiber2: start" initially; then
 - we suspend fiber2
 - that brings us back to our parent = fiber1
 - print "fiber1: END"
 - fiber1 suspends itself, that switches back to main
 - print "main2"
 - main resumes fiber2 which executes last print
 - fiber2 prints "fiber2: END", ends execution by suspending itself
 - fiber2 suspend brings back to main
 - main prints "main3"

which prints:

```
main1
fiber1: start
fiber2: start
fiber1: END
main2
fiber2: END
main3
```

so, while we used Win32 Fibers to implement asymmetric coroutines:
 
 - we can still freely switch between Fiber(s)
 - it does not matter if Fiber was resumed/activated from
   a thread (main) or the other Fiber
 - the act of switching to/resuming is to give a possibility to execute
 - this is similar to C++20 coroutines with its `.resume()`
 - nothing magically "runs" in the background; there should be a
   scheduler that resumes or switches between fibers;
   same is true for C++20 coroutines
 - we were able to interlieve execution of 3 fibers: main, fiber1, fiber2 -
   all within one system thread; there are no multiple other threads
 - fibers are executed concurrently within main thread

Note, how while executing `fiber2` we (a) were resumed from 2 different contexts
and (b) changed the parent in meantime, switching to different contexts:

``` cpp {.numberLines}
fiber2._callback = [self = &fiber2]()
{
    // ... from fiber1
    self->suspend(); // switch back to fiber1 (=our parent)
    // ... from main
    self->suspend(); // switch to main (=our parent)
};
```

## tasks for Fibers

C++20 coroutines allow for a function to return a value:

``` cpp {.numberLines}
co::Tast<int> MyCoroutine()
{
    co_await Request();
    co_return 1;
}

int main()
{
    co::Tast<int> task = MyCoroutine();
    // use a task
}
```

We need to build something similar on top of `Fiber` class:

``` cpp {.numberLines}
int MyFiber()
{
    this_fiber::suspend();
    return 1;
}

int main()
{
    FiberTask<int> task = FF_async([] { return MyFiber(); });
    // use a task
}
```

Note how:

 - any mention of a `Fiber` goes away
 - there is nice interface to launch a new Fiber
 - we can suspend itself within an execution context; and
 - `MyFiber()` simply returns naked `int`;
   there is no need to mark every coroutine with task-like return type.

In addition, under the hood:

 - we use Fibers pool to preallocate N fibers and reuse them
 - there is simple Fibers scheduler to drive our Fibers execution
 - use exceptions to allow cancellable fiber tasks

Last one is presented just to showcase one of the possibilities;
not required for CURL_async_get() Fiber wrapper.

## cancellable Fiber task with a generic callback

CODE: CH0x_fiber_callback

First, we start with replacing hardcoded `Fiber::run()` with a generic
version that uses next interface:

``` cpp {.numberLines}
// To separate `FiberTask` implementation from an actual `Fiber`.
struct FiberCallbackBase
{
    void run()    { return do_run();    }
    void resume() { return do_resume(); }

    FiberCallbackBase() noexcept = default;
    FiberCallbackBase(const FiberCallbackBase& rhs) = delete;
protected:
    ~FiberCallbackBase() noexcept = default;
    virtual void do_run() = 0;
    virtual void do_resume() {} // optional
};
```

Where we allow a user to `run()` anything and also allow to
be notified that Fiber is resumed. This is needed to implement
a task cancelation: when canceled, user's defined `resume()`
throws an exception to unwind everything in a context of fiber
execution. Hence, our `run()` implementation becomes:

``` cpp {.numberLines}
static void FiberProc(void* parameter)
{
    Fiber& self = *static_cast<Fiber*>(parameter);
    while (true)
    {
        self.run();
        self.suspend();
    }
}

void Fiber::run()
{
    try
    {
        _callback->resume();
        _callback->run();
    }
    catch (...)
    {
        _exception = std::current_exception();
    }
    _callback = nullptr;
}
```

where `_callback` and `_exception` are:

``` cpp {.numberLines}
struct Fiber
{
    void* _fiber = nullptr;
    void* _parent_fiber = nullptr;
    FiberCallbackBase* _callback = nullptr;
    std::exception_ptr _exception;
    // ...
};
```

so, when `run()` enters we (a) notify a user it got
resumed (first time) and (b) run everything. If exception
is thrown, we just remember it and clean-up our current
`_callback` - the execution is done and we go to suspend.

What is `_callback`? This is something user can set on
a `Fiber` instance allocated from a `FiberPool` (not shown yet).
This is going to be done by a `FiberTask` under the hood.
For now, we do everything manually:

``` cpp {.numberLines}
void Fiber::set_callback(FiberCallbackBase& callback)
{
    assert(_callback == nullptr);
    _callback = &callback;
    _exception = {};
}

struct MyCallback : FiberCallbackBase
{
    virtual void do_run() override
    {
        std::println("fiber1");
    }
};

int main()
{
    Fiber::Boot _;

    Fiber fiber;
    MyCallback callback;
    fiber.set_callback(callback);
    std::println("main1");
    fiber.resume();
    std::println("main2");
}
```

For now, we just made everything we had before more complicated.
But the difference is that `Fiber::run()` now is generic and
can run anything user-defined.

To support exceptions (cancellation) - the rest of `FiberCallbackBase` interface
 - our old implementation for suspend() needs to be tweak:

``` cpp {.numberLines}
void suspend()
{
    assert(_parent_fiber);
    void* switch_to_fiber = _parent_fiber;
    _parent_fiber = nullptr;
    ::SwitchToFiber(switch_to_fiber);
    assert(_callback);
    _callback->resume();
}
```

So once `::SwitchToFiber()` returns - meaning other Fiber was
running and we are resumed, we notify a user on a new `resume()`.

Finally, to check that Fiber is doing something (either running or suspended
from within a user code), we expose next function:

``` cpp {.numberLines}
bool is_busy() const
{
    if (_exception)
    {
        std::rethrow_exception(_exception);
    }
    return !!_callback;
}
```

It does cover 2 things: (1) allows to see Fiber has valid user callback
and (2) allows to throw any Fiber exception to a user. A bit weird, but
does the job.

Overall, the `Fiber` use for a single Task - becomes:

1. allocate a new Fiber from a pool
2. set a new callback
3. resume/run the fiber to an end (is_busy() == false)
4. return Fiber to the pool
5. repeat for a new Task

Note, that from within a Task or FiberCallbackBase, there is no access
to a Fiber. How can we suspend a Task then? Following `std::this_thread`
convention, we have:

``` cpp {.numberLines}
namespace this_fiber
{
void suspend()
{
    assert(::IsThreadAFiber());
    void* fiber_data = ::GetFiberData();
    assert(fiber_data);
    Fiber& self = *static_cast<Fiber*>(fiber_data);
    self.suspend();
}
} // namespace this_fiber
```

That allows to simply do `this_fiber::suspend()` to give up task
execution and be re-scheduled later.

Linking all the pieces together, low-level Fiber Task may look like this:

``` cpp {.numberLines}
struct MyFiberTask : FiberCallbackBase
{
    bool _cancel = false;

    virtual void do_run() override
    {
        std::println("fiber1");
        this_fiber::suspend();
        std::println("fiber2");
    }

    virtual void do_resume() override
    {
        if (_cancel)
        {
            _cancel = false;
            throw std::exception("canceled");
        }
    }
};

int main()
{
    Fiber::Boot _;

    Fiber fiber;
    MyFiberTask task;
    fiber.set_callback(task);
    std::println("main1");
    fiber.resume();
    std::println("main2");
    task._cancel = true;
    fiber.resume();
    std::println("main3");
}
```

This is going to be wrapped into nice `FF_async()` interface later.
Interesting bit here is that we cancel fiber task in the middle
of its execution and "fiber2" console line is not printed:

```
main1
fiber1
main2
main3
```

## basic FiberPool

CODE: CH0x_fiber_task

The idea is simple: fiber task can be small and short-lived,
there is no point allocating (and dealocating) a
system Fiber every time task is created.
Hence, we preallocate a pool of N (=128) system
Fibers that are always alive and reuse them. That means:

 - there can be up to N Fiber(s) alive; and
 - all system resources are allocated once and never released

`FiberPool` interface may look like this:

``` cpp {.numberLines}
struct FiberPool
{
    using Handle = std::size_t;
    explicit FiberPool(std::size_t size) noexcept

    Handle allocate(FiberCallbackBase& callback);
    void free(Handle handle);

    void suspend(Handle handle);
    void resume(Handle handle);
    bool is_busy(Handle handle) const;

private:
    std::unique_ptr<Fiber[]> _fibers;
    std::size_t _size = 0;
};
```

where `Fiber` instance is hidden from a user and exposed only
with a `Handle`. So we can manage an array of fixed size internally:

``` cpp {.numberLines}
explicit FiberPool::FiberPool(std::size_t size) noexcept
{
    assert(size > 0);
    _fibers.reset(new(std::nothrow) Fiber[size]);
    assert(_fibers.get());
    _size = size;
}
```

To create a Fiber is to invoke an `allocate()`. Note, we go as
dumb and as simple as possible - doing linear search to find
first free Fiber. It all could be made better, having basic free list
allocator for indices as one way to go about it:

``` cpp {.numberLines}
Handle FiberPool::allocate(FiberCallbackBase& callback)
{
    auto find_first_free_handle = [this]()
    {
        for (std::size_t i = 0; i < _size; ++i)
        {
            if (_fibers[i]._callback == nullptr)
            {
                return Handle(i);
            }
        }
        assert(false);
        return Handle(-1);
    };
    const Handle handle = find_first_free_handle();
    Fiber& fiber = _fibers[handle];
    fiber.set_callback(callback);
    return handle;
}
```

`FiberPool::free()` is, basically, no-op. We asset `Fiber`
returned is in "complete" state - finished execution:

``` cpp {.numberLines}
void FiberPool::free(Handle handle)
{
    assert(handle < _size);
    assert(_fibers[handle]._callback == nullptr);
}
```

Next, `suspend()`, resume() and is_busy() are just a
wrappers around Fiber API, but for a `Handle`:

``` cpp {.numberLines}
void FiberPool::suspend(Handle handle)
{
    assert(handle < _size);
    _fibers[handle].suspend();
}
```

With all this, our previous example running fiber task becomes:

``` cpp {.numberLines}
int main()
{
    Fiber::Boot _;
    FiberPool fiber_pool{128};

    MyFiberTask task;
    FiberPool::Handle fiber = fiber_pool.allocate(task);
    fiber_pool.resume(fiber);
    fiber_pool.free(fiber);
}
```

Now, lets get rid of manually created MyFiberTask and make
`FiberTask<T>` possible.

## FiberTask

CODE: CH0x_fiber_task

We'd like to be able to write something like this:

``` cpp {.numberLines}
int MyFiber()
{
    this_fiber::suspend();
    return 1;
}

int main()
{
    FiberTaskScheduler scheduler;
    FiberTask<int> task = FF_async(scheduler, [] { return MyFiber(); });
    // ...
    // scheduler.schedule();
}
```

There are several moving parts:

1. we need type-erased implementation of FiberTask that can
   consume any user-defined lambda/callback, work with any return type `T`; and
2. we need a scheduler that knows how to drive tasks/fibers
   execution

`FiberTask<T>` allocates a Fiber, runs a given callable/lambda and stores
the result. Given FiberTask can be destroyed by a user any time, the way
we handle this is to ensure that actual FiberTask state is owned
by `FiberTaskScheduler`: if FiberTask is destroyed while still running,
we cancel `Fiber` and free it later, once the execution is done.

Interestingly, there could be up to N active Fibers (FiberPool capacity),
but FiberTasks count owned by a user can be larger. Consider next code:

``` cpp {.numberLines}
FiberTask<void> m_tasks[N];
for (...) { m_tasks[i] = FF_async(...) }
// N Fiber tasks completes
// 
// User still owns N m_tasks and may query
// the status and result - any time later.
```

Going with more optimized implementation, we could:

 - preallocate N system Fibers
 - preallocate a memory for up to M (>=N) fiber Tasks
 - each fiber Task could be limited in size (let 256 bytes); and
 - each Task could free Fiber as soon as task ends

i.e., Fiber and Task lifetime could be different and
everything is preallocated; meaning no allocations at runtime.

For simpler implementation, we (a) bind Fiber lifetime to a Task lifetime:
Fiber is released only when Task is destroyed; and (b) Task
allocates on creation, going with more standard code for type-erased
implementation.

With this in mind, we start with FiberTask_Any:

``` cpp {.numberLines}
struct FiberTask_Any : public FiberCallbackBase
{
    explicit FiberTask_Any(FiberPool& fiber_pool);
    virtual ~FiberTask_Any() noexcept;

    bool is_completed() const;
    void execute();
    void cancel();

private:
    virtual void do_resume() override;

protected:
    FiberPool& _fiber_pool;
    FiberPool::Handle _fiber;
    bool _cancelled = false;
};
```

that implements FiberCallbackBase interface and
can be cancelled, but knows nothing on how to
store the result of execution:

``` cpp {.numberLines}
FiberTask_Any::FiberTask_Any(FiberPool& fiber_pool)
    : _fiber_pool(fiber_pool)
    , _fiber(fiber_pool.allocate(*this))
{
}
FiberTask_Any::~FiberTask_Any() noexcept
{
    assert(is_completed());
    _fiber_pool.free(_fiber);
}
```

For cancellation, we throw an exception when Fiber is
resumed:

``` cpp {.numberLines}
class Exception_FiberTaskCancelled : public std::exception
{
};

void FiberTask_Any::cancel()
{
    assert(_cancelled == false);
    _cancelled = true;
}

void FiberTask_Any::do_resume()
{
    if (_cancelled)
    {
        _cancelled = false;
        throw Exception_FiberTaskCancelled{};
    }
}
```

To execute task is to resume a Fiber:

``` cpp {.numberLines}
void FiberTask_Any::execute()
{
    assert(is_completed() == false);
    _fiber_pool.resume(_fiber);
}
```

Finally, `is_completed()` check looks for a Fiber
that ended the execution and/or has an exception thrown:

``` cpp {.numberLines}
bool FiberTask_Any::is_completed() const
{
    try
    {
        return (_fiber_pool.is_busy(_fiber) == false);
    }
    catch (...)
    {
    }
    return true;
}
```

To handle void and non-void return types, we introduce FiberTask_WithResult:

``` cpp {.numberLines}
template<typename R>
struct FiberTask_WithResult : public FiberTask_Any
{
public:
    using FiberTask_Any::FiberTask_Any;
    const R& get() const;
    R&& get_once()
    {
        const R& r = static_cast<const FiberTask_WithResult&>(*this).get();
        return std::move(const_cast<R&>(r));
    }
protected:
    // In case return type is not DefaultConstructiable.
    std::variant<std::monostate, R> _storage;
};

template<>
struct FiberTask_WithResult<void> : public FiberTask_Any
{
public:
    using FiberTask_Any::FiberTask_Any;
    void get() const;
    void get_once()
    {
        return static_cast<const FiberTask_WithResult&>(*this).get();
    }
};
```

where non-void FiberTask places the result in the `_storage` data member.
`get_once()` is just a `get()` that give rvalue reference back so
it could be moved from. `get()` implementation is just:

``` cpp {.numberLines}
const R& get() const
{
    // Populate exception, if any.
    const bool running = _fiber_pool.is_busy(_fiber);
    assert(running == false);
    assert(_storage.index() == 1);
    return std::get<1>(_storage);
}
```

Note how we must check Fiber's `is_busy()` to populate any exception
to the user if something was throws during Fiber execution.

Finally, to run a lambda, we go with FiberTask_Callable:

``` cpp {.numberLines}
template<typename R, typename C>
struct FiberTask_Callable : public FiberTask_WithResult<R>
{
    using Base = FiberTask_WithResult<R>;
public:
    explicit FiberTask_Callable(C&& callable, FiberPool& fiber_pool)
        : Base(fiber_pool)
        , _callable(std::move(callable))
    {
    }
private:
    virtual void do_run() override
    {
        if constexpr (std::is_same_v<void, R>)
        {
            _callable();
        }
        else
        {
            this->_storage.template emplace<1>(_callable());
        }
    }
private:
    C _callable;
};
```

FiberTask_Callable is what's going to be allocated on every `FF_async()`
call that creates a `FiberTask<T>`:

``` cpp {.numberLines}
template<typename R>
struct FiberTask
{
    using Task = FiberTask_WithResult<R>;

    template<typename C>
    explicit FiberTask(C&& callable, FiberTaskScheduler& scheduler) noexcept
        : _scheduler(&scheduler)
    {
        using TaskCallable = FiberTask_Callable<R, std::remove_cvref_t<C>>;
        TaskCallable* task = new(std::nothrow) TaskCallable(
            std::forward<C>(callable), scheduler._fiber_pool);
        assert(task);
        scheduler.add_fiber_task(std::unique_ptr<FiberTask_Any>(task));
        _task = task;
    }
    ~FiberTask() noexcept
    {
        destroy_once();
    }
    void destroy_once() noexcept
    {
        if (_scheduler)
        {
            assert(_task);
            _scheduler->remove_fiber_task(*_task);
            _scheduler = nullptr;
            _task = nullptr;
        }
    }
    decltype(auto) get() const;
    decltype(auto) get_once();
// ...
    FiberTaskScheduler* _scheduler = nullptr;
    Task* _task = nullptr;
};

template<typename C>
auto FF_async(FiberTaskScheduler& scheduler, C&& callable)
{
    using Task = FiberTask<std::invoke_result_t<C>>;
    return Task{std::forward<C>(callable), scheduler};
}
```

`_task` itself is owned by a FiberTaskScheduler:

``` cpp {.numberLines}
struct FiberTaskScheduler
{
    FiberPool& _fiber_pool;
    std::vector<std::unique_ptr<FiberTask_Any>> _tasks;
    std::vector<FiberTask_Any*> _tasks_to_remove;

    void add_fiber_task(std::unique_ptr<FiberTask_Any>&& task)
    {
        assert(task.get());
        _tasks.push_back(std::move(task));
    }
    void remove_fiber_task(FiberTask_Any& task)
    {
        if (task.is_completed() == false)
        {
            task.cancel();
        }
        _tasks_to_remove.push_back(&task);
    }
```

Note how removing a task is also a cancel if needed.

We are left with `FiberTaskScheduler` execution, which is its `schedule()`:

``` cpp {.numberLines}
void FiberTaskScheduler::schedule()
{
    while (schedule_once()) {}
}

bool FiberTaskScheduler::schedule_once()
{
    bool repeat = false;
    auto tasks = std::move(_tasks);
    for (auto it = tasks.rbegin(); it != tasks.rend(); ++it)
    {
        std::unique_ptr<FiberTask_Any>& task = *it;
        assert(task);
        if (task->is_completed() == false)
        {
            task->execute();
            repeat |= task->is_completed();
            continue;
        }
        auto it_remove = std::find(_tasks_to_remove.begin()
            , _tasks_to_remove.end(), task.get());
        if (it_remove == _tasks_to_remove.end())
        {
            continue;
        }
        task.reset();
    }
    for (std::unique_ptr<FiberTask_Any>& task : tasks)
    {
        if (task)
        {
            _tasks.push_back(std::move(task));
        }
    }
    return repeat;
}
```

There are 2 moments worth mentioning:

1. tasks are executed from the end - this is needed to execute
   child tasks first so parent tasks that wait can have progress
2. if any task is completed, we schedule a loop again - again
   so any parent tasks can complete if child tasks complete

Other then that, we just `.execute()` every task and remove it
when completed and was removed.

Finally, running a FiberTask is:

``` cpp {.numberLines}
int MyFiber()
{
    this_fiber::suspend();
    return 1;
}

int main()
{
    Fiber::Boot _;
    FiberPool fiber_pool{128};
    FiberTaskScheduler fibers_scheduler{fiber_pool};

    FiberTask<int> task = FF_async(fibers_scheduler, &MyFiber);
    while (task.is_completed() == false)
    {
        fibers_scheduler.schedule();
    }
    std::println("{}", task.get()); // prints "1"
}
```

## waiting for a CURL request with a Fiber

CODE: CH0x_fiber_await_curl

Assuming we are withing a Fiber context already:

``` cpp {.numberLines}
void Fiber_Main(CURL_Async curl_async)
{
    const std::string r = CURL_fiber_get_V1(curl_async, "localhost:5001/file1.txt");
    std::println("{}", r);
}
```

Lets write our CURL_fiber_get_V1():

``` cpp {.numberLines}
std::string CURL_fiber_get_V1(CURL_Async curl_async, const std::string& url)
{
    std::optional<std::string> response;
    CURL_async_get(curl_async, url, &response
        , [](void* user_data, std::string response)
    {
        auto& state = *static_cast<std::optional<std::string>*>(user_data);
        state.emplace(std::move(response));
    });
    while (response.has_value() == false)
    {
        this_fiber::suspend();
    }
    return std::move(response.value());
}
```

In a way - it's trivial:

1) we start a GET request
2) we wait for a data in a while loop
3) while loop suspends itself if no data yet
4) we use a pointer to local `response` variable since it's valid until Fiber end

Note: compiler can't see or prove that local variable `response` is NOT
modified externally, hence the loop is not an infinite loop. There is no need to use
volatile/atomic to sidestep the compiler.

Given this - all good... Until someone CANCELS our Fiber Task.
Remember, user has a reference to a FiberTask:

``` cpp {.numberLines}
FiberTask<void> task = FF_async(fibers_scheduler, &Fiber_Main, curl_async);
```

What if user drops/destroys a task while GET request is still in progress?

``` cpp {.numberLines}
FiberTask::~FiberTask()
{
    assert(_task);
    _scheduler->remove_fiber_task(*_task);

}
```

And remove_fiber_task() just cancels the Task in progress:

``` cpp {.numberLines}
void FiberTaskScheduler::remove_fiber_task(FiberTask_Any& task)
{
    if (task.is_completed() == false)
    {
        task.cancel();
    }
    _tasks_to_remove.push_back(&task);
}
```

What happens to cancelled Fiber? There are 2 options:

1) we destroy a Fiber, deallocating a memory/system Fiber; and
2) we keep a Fiber in a pool, leaving the memory alive.

Since we have a FiberPool, cancelling a Task/Fiber does not deallocate the memory
since system Fiber is still alive. So, here:

``` cpp {.numberLines}
std::optional<std::string> response;
CURL_async_get(curl_async, url, &response
    , [](void* user_data, std::string response)
{
    auto& state = *static_cast<std::optional<std::string>*>(user_data);
    state.emplace(std::move(response));
});
```

when callback is invoked and Fiber was/is cancelled, our `response` local variable
is still alive and there is no access error to deallocated memory. So.. no issue?

Still, while the memory/Fiber is not deallocated, logically, it's an access to
released resource and in practice, in our case - someone else could start another
Fiber task that so happens gets the same Fiber. Hence, our CURL_async_get()
callback overrides and corrupts the memory of some other task!

Sadly, cancellation is a problem at the end. To handle that - since CURL_async_get()
is not cancellable, we need to allocate a flag/state on a heap so it can outlive
cancelled Fiber:

``` cpp {.numberLines}
std::string CURL_fiber_get(CURL_Async curl_async, const std::string& url)
{
    std::optional<std::string> response;

    CancelToken* cancel_token = CancelToken::Make(&response);
    cancel_token->add_ref();

    CURL_async_get(curl_async, url, cancel_token
        , [](void* user_data, std::string response)
    {
        CancelToken& cancel_token = *static_cast<CancelToken*>(user_data);
        if (auto* state = cancel_token.as<std::optional<std::string>>())
        {
            state->emplace(std::move(response));
        }
        cancel_token.release();
    });

    try
    {
        while (response.has_value() == false)
        {
            this_fiber::suspend();
        }
        cancel_token->release();
    }
    catch (...) // including Exception_FiberTaskCancelled
    {
        cancel_token->reset();
        cancel_token->release();
        throw;
    }

    return std::move(response.value());
}
```

Logically, we:

1) allocate CancelToken, which is ref-counted and there are 2 users:
   callback and the task itself
3) remember a pointer to a local variable `response`
3) when task is cancelled (exception thrown) - we reset the reference to `response`
4) when callback is invoked, we check if `response` is still alive;
   CancelToken is guaranteed to be valid since it's ref-counted.

CancelToken implementation is just:

``` cpp {.numberLines}
struct CancelToken
{
    std::int64_t _ref_count = 0;
    void* _user_data = nullptr;
    static CancelToken* Make(void* user_data)
    {
        CancelToken* token = new(std::nothrow) CancelToken;
        assert(token);
        token->_ref_count = 1;
        token->_user_data = user_data;
        return token;
    }
    void add_ref()
    {
        _ref_count += 1;
    }
    void release()
    {
        _ref_count -= 1;
        assert(_ref_count >= 0);
        if (_ref_count == 0)
        {
            Destroy(this);
        }
    }
    static void Destroy(CancelToken* token)
    {
        assert(token);
        delete token;
    }
    void reset()
    {
        _user_data = nullptr;
    }
    template<typename T>
    T* as() const
    {
        return static_cast<T*>(_user_data);
    }
};
```

With that in mind, complete CURL_fiber_get() within a FiberTask is:

``` cpp {.numberLines}
void Fiber_Main(CURL_Async curl_async)
{
    const std::string r1 = CURL_fiber_get(curl_async, "localhost:5001/file1.txt");
    const std::string r2 = CURL_fiber_get(curl_async, "localhost:5001/file2.txt");
    std::println("{}", r1);
    std::println("{}", r2);
}

int main()
{
    Fiber::Boot _;
    FiberPool fiber_pool{8};
    FiberTaskScheduler fibers_scheduler{fiber_pool};
    CURL_Async curl_async = CURL_async_create();
    FiberTask<void> task = FF_async(fibers_scheduler, &Fiber_Main, curl_async);
    while (task.is_completed() == false)
    {
        CURL_async_tick(curl_async);
        fibers_scheduler.schedule();
    }
    CURL_async_destroy(curl_async);
}
```

Note, FF_async() was extended to accept extra arguments.
Complete code is in CH0x_fiber_await_curl.

## waiting for multiple a FiberTasks

CODE: CH0x_fiber_await_many

Given a list of FiberTasks, we just wait for completion in the loop:

``` cpp {.numberLines}
template<typename... Ts>
std::tuple<Ts...> FF_await_all(FiberTask<Ts>... tasks)
{
    auto wait_task = [](auto task) -> auto
    {
        while (task.is_completed() == false)
        {
            this_fiber::suspend();
        }
        return task.get_once();
    };
    return {wait_task(std::move(tasks))...};
}
```

Here, we intentionally accept `tasks` by value so that all the tasks are consumed
and, more importantly, if await is cancelled, all of the tasks are also discarded
due to stack unwinding.

To be able to wait for multiple CURL requests, we need to have FiberTask. We do:

``` cpp {.numberLines}
FiberTask<std::string> CURL_fiber_get(
      CURL_Async curl_async
    , const std::string& url
    , FiberTaskScheduler& fiber_scheduler)
{
    return FF_async(fiber_scheduler, [=]()
    {
        return CURL_fiber_get(curl_async, url);
    });
}
```

Finally, doing 2 concurrent requests with Fibers is:

``` cpp {.numberLines}
void Fiber_Main(FiberTaskScheduler* fiber_scheduler, CURL_Async curl_async)
{
    auto [r1, r2] = FF_await_all(
        CURL_fiber_get(curl_async, "localhost:5001/file1.txt", *fiber_scheduler),
        CURL_fiber_get(curl_async, "localhost:5001/file2.txt", *fiber_scheduler)
        );
    std::println("{}", r1);
    std::println("{}", r2);
}
```

We need `FiberTaskScheduler` to create children Fiber tasks, so it's a bit more
lengthy to type all that compared with the same code for C++20 coroutines.

It's possible to get rid of `fiber_scheduler` by having it implicitly available
as a global thread local variable, so CURL_fiber_get()/or FF_async() becomes:

``` cpp {.numberLines}
FiberTask<std::string> CURL_fiber_get(
      CURL_Async curl_async
    , const std::string& url
    , FiberTaskScheduler& fiber_scheduler = FiberTaskScheduler::get_current())
```

However, that's mostly irrelevant in our context.

# building std::future API

CODE: CH0x_future

Having CURL_async_get() leads to the next implementation that wraps everything into
a std::future:

``` cpp {.numberLines}
std::future<std::string> CURL_future_get(
    CURL_Async curl_async, const std::string& url)
{
    using Promise = std::promise<std::string>;
    Promise* promise = new(std::nothrow) Promise;
    assert(promise);
    std::future<std::string> future = promise->get_future();
    CURL_async_get(curl_async, url, promise
        , [](void* user_data, std::string response)
    {
        Promise* promise = static_cast<Promise*>(user_data);
        promise->set_value(std::move(response));
        delete promise;
    });
    return future;
}
```

Everything is just what std:: exposes. Note how we need to allocate our
promise so it can be alive until the end of the request.

All in all, we:

 1. allocate `std::promise`
 2. `std::promise` allocates shared state, used by std::future
 3. CURL_async_get allocates `std::string` to write a response
 4. CURL_async_get allocates `std::function` for a generic callback
 5. CURL_async_get allocates `std::unordered_map` node to remember
    what to call when
 6. .. and probably something else (CURL internals, etc)

Anyway, std::future is missing useful bits to work with it in a non-blocking manner:

 - no built-in `.is_ready()` check
 - no built-in `.then()` continuation,
   see [Design and evolution of C++ future continuations](https://ikriv.com/blog/?p=4916)

Faking `is_future_ready()` with:

``` cpp {.numberLines}
template<typename T>
bool is_future_ready(const std::future<T>& future)
{
    const std::future_status status = future.wait_for(std::chrono::seconds::zero());
    return (status == std::future_status::ready);
}
```

allows to finally write something among the lines:

``` cpp {.numberLines}
int main()
{
    CURL_Async curl_async = CURL_async_create();
    std::future<std::string> result = CURL_future_get(
        curl_async, "localhost:5001/file1.txt");
    while (is_future_ready(result) == false)
    {
        CURL_async_tick(curl_async);
    }
    CURL_async_destroy(curl_async);
    std::println("async future response: '{}'", result.get());
}
```

Missing std::future features makes it not composable; waiting 2 tasks to finish
is the same as checking 2 flags to become true; giving not much of a win compared to
direct use of CURL_async_get().

See App_Futures, App_Callbacks.

# building future-like task API with continuation support (.then())

CODE: CH0x_task_basic

Lets build std::future-like Task type that supports `.then()` continuations.

Everything is the same as std::future, but there is a support to chain operations,
see [std::experimental::future::then()](https://en.cppreference.com/cpp/experimental/future/then)
and [boost::future::then()](https://www.boost.org/doc/libs/latest/doc/html/thread/synchronization.html#thread.synchronization.futures.then):

``` cpp {.numberLines}
Task<std::string> CURL_task_get(CURL_Async curl_async, const std::string& url);

int main()
{
    Task<void> task = CURL_task_get(curl_async, "localhost:5001/file1.txt")
        .then([](std::string response)
    {
        std::println("{}", response);
    });
    // ...
}
```


# senders

# reactive streams

# SAMPLES

## synchronous requests {#app_blocking}

CODE: App_Blocking

For completeness, our trivial case - doing 2 GET requests sequentially:

``` cpp {.numberLines}
static void App_Blocking()
{
    const std::string r1 = CURL_get("localhost:5001/file1.txt");
    const std::string r2 = CURL_get("localhost:5001/file2.txt");
    std::println("{}", r1);
    std::println("{}", r2);
}

int main()
{
    App_Blocking();
}
```

## requests with callbacks {#app_callbacks}

CODE: App_Callbacks

With callbacks API, there are 2 variations:

1. doing 2 sequential requests, one after another: see `App_CallbacksV0()`
2. doing 2 requests concurrently: see `App_CallbacksV1()`

``` cpp {.numberLines}
int main()
{
    App_CallbacksV0(); // sequential
    App_CallbacksV1(); // concurrent
}
```

All the other sections have the same naming convention and the same main().

SEQUENTIAL requests:

``` cpp {.numberLines}
struct App_StateV0 // sequential
{
    CURL_Async _curl_async{};
    bool _finished = false;
    std::string _r1;
    std::string _r2;

    explicit App_StateV0(CURL_Async curl_async) noexcept
        : _curl_async(curl_async)
    {
    }
    App_StateV0(const App_StateV0&) = delete;
    ~App_StateV0() noexcept
    {
        assert(_finished);
    }

    void start()
    {
        CURL_async_get(_curl_async, "localhost:5001/file1.txt", this
            , [](void* user_data, std::string response1)
        {
            App_StateV0& state = *static_cast<App_StateV0*>(user_data);
            state._r1 = std::move(response1);

            CURL_async_get(state._curl_async, "localhost:5001/file2.txt", user_data
                , [](void* user_data, std::string response2)
            {
                App_StateV0& state = *static_cast<App_StateV0*>(user_data);
                state._r2 = std::move(response2);
                state._finished = true;
                state.done();
            });
        });
    }

    void done()
    {
        assert(_finished);
        std::println("{}", _r1);
        std::println("{}", _r2);
    }
};

static void App_CallbacksV0()
{
    CURL_Async curl_async = CURL_async_create();
    App_StateV0 app{curl_async};
    app.start();
    while (!app._finished)
    {
        CURL_async_tick(curl_async);
    }
    CURL_async_destroy(curl_async);
}
```

CONCURRENT requests:

``` cpp {.numberLines}
struct App_StateV1 // concurrent
{
    CURL_Async _curl_async{};
    std::int32_t _requests = 0;
    bool _finished = false;
    std::string _r1;
    std::string _r2;

    explicit App_StateV1(CURL_Async curl_async) noexcept
        : _curl_async(curl_async)
    {
    }
    App_StateV1(const App_StateV1&) = delete;
    ~App_StateV1() noexcept
    {
        assert(_finished);
    }

    void start()
    {
        _requests = 2;
        CURL_async_get(_curl_async, "localhost:5001/file1.txt", this
            , [](void* user_data, std::string response)
        {
            App_StateV1& state = *static_cast<App_StateV1*>(user_data);
            state._r1 = std::move(response);
            state.try_finish();
        });
        CURL_async_get(_curl_async, "localhost:5001/file2.txt", this
            , [](void* user_data, std::string response)
        {
            App_StateV1& state = *static_cast<App_StateV1*>(user_data);
            state._r2 = std::move(response);
            state.try_finish();
        });
    }

    void try_finish()
    {
        assert(_requests > 0);
        _requests -= 1;
        if (_requests == 0)
        {
            _finished = true;
            done();
        }
    }

    void done()
    {
        assert(_finished);
        std::println("{}", _r1);
        std::println("{}", _r2);
    }
};

static void App_CallbacksV1()
{
    CURL_Async curl_async = CURL_async_create();
    App_StateV1 app{curl_async};
    app.start();
    while (!app._finished)
    {
        CURL_async_tick(curl_async);
    }
    CURL_async_destroy(curl_async);
}
```

## requests with coroutines {#app_coroutines}

CODE: App_Coroutines

SEQUENTIAL requests:

``` cpp {.numberLines}
static Co_Task<void> Coro_MainV0(CURL_Async curl_async) // sequential
{
    const std::string r1 = co_await CURL_await_get(curl_async, "localhost:5001/file1.txt");
    const std::string r2 = co_await CURL_await_get(curl_async, "localhost:5001/file2.txt");
    std::println("{}", r1);
    std::println("{}", r2);
}

static void App_CoroutinesV0()
{
    CURL_Async curl_async = CURL_async_create();
    Co_Task<void> task = Coro_MainV0(curl_async);
    task.resume();
    while (task.is_in_progress())
    {
        CURL_async_tick(curl_async);
    }
    CURL_async_destroy(curl_async);
}
```

main() loop takes a bit of space, but the actual business logic is almost the
same as regular synchronous code.

CONCURRENT requests:

``` cpp {.numberLines}
static Co_Task<void> Coro_MainV1(CURL_Async curl_async) // concurrent
{
    auto [r1, r2] = co_await CO_await_all(
        CURL_coro_get(curl_async, "localhost:5001/file1.txt"),
        CURL_coro_get(curl_async, "localhost:5001/file2.txt"));
    std::println("{}", r1);
    std::println("{}", r2);
}

static void App_CoroutinesV1()
{
    CURL_Async curl_async = CURL_async_create();
    Co_Task<void> task = Coro_MainV1(curl_async);
    task.resume();
    while (task.is_in_progress())
    {
        CURL_async_tick(curl_async);
    }
    CURL_async_destroy(curl_async);
}
```

## requests with fibers {#app_fibers}

CODE: App_Fibers

SEQUENTIAL requests:

``` cpp {.numberLines}
static void Fiber_MainV0(CURL_Async curl_async) // sequential
{
    const std::string r1 = CURL_fiber_get(curl_async, "localhost:5001/file1.txt");
    const std::string r2 = CURL_fiber_get(curl_async, "localhost:5001/file2.txt");
    std::println("{}", r1);
    std::println("{}", r2);
}

static void App_FibersV0()
{
    Fiber::Boot _;
    FiberPool fiber_pool{8};
    FiberTaskScheduler fibers_scheduler{fiber_pool};
    CURL_Async curl_async = CURL_async_create();
    FiberTask<void> task = FF_async(fibers_scheduler
        , &Fiber_MainV0, curl_async);
    while (task.is_completed() == false)
    {
        CURL_async_tick(curl_async);
        fibers_scheduler.schedule();
    }
    CURL_async_destroy(curl_async);
}
```

CONCURRENT requests:

``` cpp {.numberLines}
///////////////////////////////////////////////////////////
static void Fiber_MainV1( // concurrent
    FiberTaskScheduler* fiber_scheduler, CURL_Async curl_async)
{
    auto [r1, r2] = FF_await_all(
        CURL_fiber_get(curl_async, "localhost:5001/file1.txt", *fiber_scheduler),
        CURL_fiber_get(curl_async, "localhost:5001/file2.txt", *fiber_scheduler)
        );
    std::println("{}", r1);
    std::println("{}", r2);
}

static void App_FibersV1()
{
    Fiber::Boot _;
    FiberPool fiber_pool{8};
    FiberTaskScheduler fibers_scheduler{fiber_pool};
    CURL_Async curl_async = CURL_async_create();
    FiberTask<void> task = FF_async(fibers_scheduler
        , &Fiber_MainV1, &fibers_scheduler, curl_async);
    while (task.is_completed() == false)
    {
        CURL_async_tick(curl_async);
        fibers_scheduler.schedule();
    }
    CURL_async_destroy(curl_async);
}
```
