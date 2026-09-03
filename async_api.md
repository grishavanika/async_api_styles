---
title: Asynchronous API
include-before: |

    I showcase different variations of asynchronous APIs with examples
    of using libcurl, specifically, doing 2 GET requests -
    both sequentially and concurrently.

    NO threads are involved to disconnect any associations
    of coroutines or fibers with multithreading. Something is intentionally
    simpler, while still having as much details as possible.

    Jump to [tasks](#tasks), [std::future](#future),
    [coroutines](#coroutines), [fibers](#fibers), [senders](#senders).

    [Work In Progress]{.mark}.

---

--------------------------------------------------------------------------------

# introduction {#intro}

I start with a simple C-style API on top of [libcurl C API](https://curl.se/libcurl/c/)
and have a code that may look like this:

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

The code above performs two GET requests sequentially. Everything executes synchronously.

Next, lets have simple C-style callbacks API, intentionally,
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

There is a need to have a `State` for bookkeeping, pass it as a
`void*` user data to access later and, finally, run an event loop
to give libcurl a chance to process requests. Note, however,
requests execute concurrently now, as in - 2 requests are active at the same time.

After this, lets build [tasks](#tasks), [std::future](#future),
[coroutines](#coroutines), [fibers](#fibers), [senders](#senders) and other
variations of asynchronous API on top of C-style callbacks above.

But before that, lets wrap [libcurl C API](https://curl.se/libcurl/c/)
for our needs.

--------------------------------------------------------------------------------

# setup with cmake + libcurl {#cmake}

CODE: CH00_cmake

For [vcpkg](https://github.com/microsoft/vcpkg), there is an extensive
[documentation](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started)
available. In short:

``` bash {.numberLines}
git clone https://github.com/microsoft/vcpkg
cd vcpkg
bootstrap-vcpkg.bat
```

I also set VCPKG_ROOT that points to specific full path I have (`K:\vcpkg`)
and add this path to `PATH` environemnt variable so build scripts
can use `VCPKG_ROOT` and `vcpkg` without a need to know the exact location.

``` bash {.numberLines}
set VCPKG_ROOT=K:\vcpkg
set PATH=%VCPKG_ROOT%;%PATH%
```

For the project (async_api), vcpkg [manifest mode](https://learn.microsoft.com/vcpkg/consume/manifest-mode)
is used. Together with `curl` setup, all required steps are

``` bash {.numberLines}
cd async_api
vcpkg new --application
vcpkg add port curl
```

I have async_api folder as `K:\async_api`, but it can be anywhere else.

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
cd async_api
cmake -S . -B build ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Debug
:: run a test
.\build\00_cmake_libcurl\Debug\00_cmake_libcurl.exe
```

This assumes cmake.exe is in your `PATH`, see `build.cmd`.

# building blocking API {#libcurl_easy}

CODE: CH01_libcurl_easy

Blocking, synchronous API for GET request is straightforward.
I go with a function that looks like this:

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

Given the directory that has file1.txt and file2.txt, `CURL_get("localhost:5001/file1.txt")`
should work and return the content of the file, see [blocking libcurl section](#libcurl_easy).

# building C-style callbacks API {#libcurl_multi}

CODE: CH02_libcurl_multi

## thoughts on the design {#libcurl_multi_design}

Now, lets imagine simplest possible asynchronous API. The difference to
[blocking API](#libcurl_easy) is that we ask the system to start a GET request
and the response should arrive some time later. The system invokes a
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

There are more nuances, like how frequently/when `CURL_async_tick()` should be
invoked by a user; but all this is left out of the scope.

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

CODE: CH02_libcurl_multi

For our [API](#libcurl_multi_design):

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

Now, user just needs to pass `CURL_Async` handle around.
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

There are few moving parts and issues:

 1. we create and setup curl easy handle in the same way as for blocking call;
 2. we allocate separate `std::string` to write the response data to with the
    same `CURL_OnWriteCallback` callback as in [blocking implementation](#libcurl_easy);
 3. finally, we associate the request with event loop/multi handle
 4. new `std::string` will leak the memory if the request is not completed
 5. overall, there are more hidden alocations from within `add_request()`:
    a) std::function<> most likely allocates
    b) std::unordered_map allocates

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
requests on the fly with coroutines.

Lets start with basics.

## C++ coroutines, basic task type

CODE: CH0x_coro_task

There is a trick to writing some basic C++20 coroutines code - **listen to
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
to `std::coroutine_handle<>` - the only way to interact with just alocated
coroutine. Once `Co_Task` is created, we return it to the user.
It's **up to the user** to manage `std::coroutine_handle<>`. In our case, we own just created
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

CODE: CH0x_coro_await

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

 1. knows if some operation is ready or not (`..._ready`)
 2. knows how to resume coroutine later (`..._suspend`)
 3. knows how to get the result of awaited operation (`..._resume`)

The compiler asks awaiter, specifically, `Co_CurlAsync` with
`bool await_ready()` if operation is done/ready or is in progress. If awaiter
returns false, the compiler switches current coroutine state to "suspended"
and invokes awaiter's `await_suspend(std::coroutine_handle<> coro)`
customization point which allows to remember currently suspended coroutine `coro` handle,
to call `.resume()` later, once operation is done.
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

4th solution is the most ineficient and requires no changes neither in Co_Task
nor in callback API.

## C++ coroutines, await callback (no crash)

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

Lets alocate a separate object that can outlive the coroutine/await.
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

That's how you write ineficient coroutine types for a systems
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
 5. CURL_async_get allocates `std::unordered_map` node to remember what to call when
 6. .. and probably something else (CURL internals, etc)

"simple" CURL_async_get() implementation alone brings 3 allocations.
"simple" co_await CURL_async_get() brings 2 more separate allocations.

With CURL scheduler that **knows** about coroutines and few more
optimizations and limitations, this number of allocations can go down to amortized 0:

 * CURL scheduler prealocates up to N max requests
 * request itself knows how to store the response and coroutine-callback inline
 * coroutine itself re-uses memory pool for up to N max active coroutines.

# coroutines on top polling tasks

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

 - fibers allow to suspend and resume execution at any given point inside a function
 - they are stackful coroutines, as opposed to C++20 coroutines that are stackless
 - they implement symmetric coroutines (same as C++20 coroutines);
   we'll build asymmetric coroutines on top of Fibers
 - it should be trivial to ifdef POSIX implementation

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
 - we assume `LPVOID` is `void*` so no Win32 API types are used (great for headers)
 - and, given x64, `WINAPI`/`__stdcall` can be ommited, so callbacks passed to
   Win32 API can have simple C++ declarations

That's gives us next include:

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

To make that work, specifically for Win32 API, main thread needs to become a Fiber.
This is what we do by having a simple RAII class:

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

It's possible to avoid that by calling `::ConvertThreadToFiber()` on each and every
call to `Fiber::resume()`; choose what you like more.

Given a main() above, we:

 - create a fiber, which is suspended initially
 - print "main1"
 - first call to `.resume()` switches us back to `FiberProc` that invokes `Fiber::run()`:

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
Fiber on it's own, when suspendend, switches back to its invoker/resumer.
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
   scheduler that resumes or switches between fibers; same is true for C++20 coroutines
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

What is `_callback`? This is somethig user can set on
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

So once `::SwitchToFiber()` returns - meaning other Fiber was running and we are resumed,
we notify a user on a new `resume()`.

Finally, to check that Fiber is doing something (either running or suspended from a user code),
we expose next function:

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

# fibers (WIN32) (App_Fibers)
# senders
# reactive streams

