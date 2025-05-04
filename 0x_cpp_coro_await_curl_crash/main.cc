#include <print>
#include <string>
#include <functional>
#include <unordered_map>
#include <coroutine>

#include <curl/curl.h>

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

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

// coro await
struct Co_CurlAsync;
Co_CurlAsync CURL_await_get(CURL_Async curl_async, const std::string& url);

static size_t CURL_OnWriteCallback(void* ptr, size_t size, size_t nmemb, void* data)
{
    std::string& response = *static_cast<std::string*>(data);
    response.append(static_cast<const char*>(ptr), size * nmemb);
    return (size * nmemb);
}

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

void CURL_AsyncScheduler::add_request(CURL* curl_easy, Callback on_finish)
{
    assert(on_finish);
    assert(curl_easy);
    assert(!_curl_to_callback.contains(curl_easy));
    const CURLMcode status = curl_multi_add_handle(_multi_curl, curl_easy);
    assert(status == CURLM_OK);
    _curl_to_callback[curl_easy] = std::move(on_finish);
}

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

static CURL_AsyncScheduler& CURL_scheduler(CURL_Async curl_async)
{
    CURL_AsyncScheduler* scheduler = static_cast<CURL_AsyncScheduler*>(curl_async);
    assert(scheduler);
    return *scheduler;
}

void CURL_async_tick(CURL_Async curl_async)
{
    CURL_scheduler(curl_async).tick();
}

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
        , [state, user_data, callback](CURL* curl_easy_)
    {
        long response_code = -1;
        const CURLcode status_ = curl_easy_getinfo(curl_easy_, CURLINFO_RESPONSE_CODE, &response_code);
        assert(status_ == CURLE_OK);
        assert(response_code == 200L);
        curl_easy_cleanup(curl_easy_);
        std::string data = std::move(*state);
        delete state;
        callback(user_data, std::move(data));
    });
}

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

        std::suspend_always initial_suspend()
        {
            return {};
        }

        std::suspend_always final_suspend() noexcept
        {
            return {};
        }

        void return_void()
        {
            // yeah, we return void. Nothing to do
        }
       
        void unhandled_exception()
        {
            // crash, no exceptions handling
            assert(false);
        }
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

    void resume()
    {
        assert(_coro);
        assert(!_coro.done());
        _coro.resume();
    }

    bool is_in_progress() const
    {
        assert(_coro);
        return !_coro.done();
    }

    co_handle _coro;
};

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
    Co_CurlAsync awaiter;
    awaiter._curl_async = curl_async;
    awaiter._url = url;
    return awaiter;
}

static Co_Task coro_main(CURL_Async curl_async)
{
    const std::string response = co_await CURL_await_get(
        curl_async, "localhost:5001/file1.txt");

    std::println("coro_main response: '{}'", response);
    co_return;
}

int main()
{
#if (0) // set to 1 for a crash
    CURL_Async curl_async = CURL_async_create();

    {
        Co_Task task = coro_main(curl_async);
        task.resume(); // run
    }   // **destroy**

    while (true)
    {
        CURL_async_tick(curl_async);
    }
    CURL_async_destroy(curl_async);
#else
    CURL_Async curl_async = CURL_async_create();
    Co_Task task = coro_main(curl_async);
    task.resume();
    while (task.is_in_progress())
    {
        CURL_async_tick(curl_async);
    }
    CURL_async_destroy(curl_async);
#endif
}
