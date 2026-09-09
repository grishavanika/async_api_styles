#include <print>
#include <string>
#include <functional>
#include <unordered_map>
#include <coroutine>
#include <variant>

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
    std::string* state = new(std::nothrow) std::string{};
    assert(state);
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
        assert(response_code == 200L && "RUN serve.cmd");
        curl_easy_cleanup(curl_easy_);
        std::string data = std::move(*state);
        delete state;
        callback(user_data, std::move(data));
    });
}

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

template<typename T>
struct Co_Task
{
    struct promise_type;
    using co_handle = std::coroutine_handle<promise_type>;

    struct promise_type : promise_return<T>
    {
        std::int32_t* _wait_count = nullptr;
        std::coroutine_handle<> _waiting_coro;

        Co_Task get_return_object() noexcept
        {
            return Co_Task{co_handle::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }
        auto final_suspend() noexcept
        {
            struct Final_Await : std::suspend_always
            {
                std::coroutine_handle<> await_suspend(co_handle self_coro) noexcept
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
            };
            return Final_Await{};
        }
        void unhandled_exception() noexcept
        {
            // crash, no exceptions handling
            assert(false);
        }
    };

    Co_Task(co_handle coro) noexcept
        : _coro{coro}
    {
    }
    Co_Task(Co_Task&& rhs) noexcept
        : _coro{std::exchange(rhs._coro, {})}
    {
    }
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
        assert(is_in_progress());
        _coro.resume();
    }

    bool is_in_progress() const
    {
        assert(_coro);
        return !_coro.done();
    }

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

    struct Co_Await
    {
        Co_Task<T> _task;
        std::int32_t _wait_count = 1;

        bool await_ready() noexcept
        {
            assert(_task.is_in_progress());
            return false;
        }
        // intentionally auto, not decltype(auto)
        auto await_resume() noexcept
        {
            return _task.get_once();
        }
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<> waiting_coro) noexcept
        {
            promise_type& promise = _task._coro.promise();
            promise._wait_count = &_wait_count; // 1
            promise._waiting_coro = waiting_coro;
            return _task._coro; // resume us
        }
    };

    Co_Await operator co_await() && noexcept
    {
        // consume this.
        return Co_Await{._task{std::move(*this)}};
    }

    co_handle _coro;
};

template<typename Is, typename... Ts>
struct Co_Await_All;

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
    void await_suspend(std::coroutine_handle<> waiting_coro)
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
};

template<typename... Ts>
static auto CO_await_all(Co_Task<Ts>&&... tasks)
{
    static_assert(sizeof...(Ts) > 0);
    using Is = std::index_sequence_for<Ts...>;
    return Co_Await_All<Is, Ts...>{._tasks{std::move(tasks)...}};
}

struct Co_CurlAsync
{
	struct WaitState
	{
        Co_CurlAsync* _self = nullptr;
	};
    WaitState* _wait_state = nullptr;
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
    }

    ~Co_CurlAsync()
    {
	    if (_wait_state)
	    { // CURL_async_get() is still in progress
            assert(_wait_state->_self == this);
            _wait_state->_self = nullptr; // dead
        }
        // else: CURL_async_get() is already completed
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

Co_Task<std::string> CURL_coro_get(CURL_Async curl_async, std::string url)
{
    co_return co_await CURL_await_get(curl_async, url);
}

Co_Task<void> coro_main(CURL_Async curl_async)
{
    auto [r1, r2] = co_await CO_await_all(
        CURL_coro_get(curl_async, "localhost:5001/file1.txt"),
        CURL_coro_get(curl_async, "localhost:5001/file2.txt")
        );
    std::println("{}", r1);
    std::println("{}", r2);
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
