#include <print>
#include <variant>
#include <new>
#include <functional>
#include <string>
#include <unordered_map>
#include <cstdint>

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

struct Task_RefCount
{
    std::int64_t _ref_count = 1;
    Task_RefCount() noexcept = default;
    Task_RefCount(const Task_RefCount&) = delete;

    void add_ref()
    {
        assert(_ref_count >= 0);
        _ref_count += 1;
    }

    bool release()
    {
        assert(_ref_count >= 1);
        _ref_count -= 1;
        if (_ref_count == 0)
        {
            Deallocate(this);
            return true;
        }
        return false;
    }

    static void Deallocate(Task_RefCount* ptr)
    {
        assert(ptr);
        delete ptr;
    }

    virtual ~Task_RefCount() = default;
};

struct Task_Release
{
    void operator()(Task_RefCount* ptr) noexcept
    {
        assert(ptr);
        ptr->release();
    }
};

template<typename T>
using Task_Ptr = std::unique_ptr<T, Task_Release>;

template<typename T>
struct Task_Storage : Task_RefCount
{
    std::variant<std::monostate, T> _value;
    template<typename U>
    void set_value(U&& v) noexcept
    {
        assert(has_value() == false);
        _value.template emplace<1>(std::forward<U>(v));
        finish();
    }
    bool has_value() const
    {
        return (_value.index() == 1);
    }
    const T& get() const noexcept
    {
        assert(has_value());
        return std::get<1>(_value);
    }
    T consume() noexcept
    {
        assert(has_value());
        T v{std::move(std::get<1>(_value))};
        _value.template emplace<0>();
        return v;
    }
    template<typename F>
    decltype(auto) dispatch_once(F&& f)
    {
        return std::invoke(std::forward<F>(f), consume());
    }
    virtual void finish() = 0;
};

template<typename T>
struct Task_Storage<T&> : Task_RefCount
{
    T* _value = nullptr;
    template<typename U>
    void set_value(U& v) noexcept
    {
        assert(has_value() == false);
        _value = &v;
        finish();
    }
    bool has_value() const
    {
        return (_value != nullptr);
    }
    T& get() const noexcept
    {
        assert(has_value());
        return *_value;
    }
    T& consume() noexcept
    {
        assert(has_value());
        T& v = *_value;
        _value = nullptr;
        return v;
    }
    template<typename F>
    decltype(auto) dispatch_once(F&& f)
    {
        return std::invoke(std::forward<F>(f), consume());
    }
    virtual void finish() = 0;
};

template<>
struct Task_Storage<void> : Task_RefCount
{
    bool _has_value = false;
    void set_value() noexcept
    {
        assert(has_value() == false);
        _has_value = true;
        finish();
    }
    bool has_value() const
    {
        return _has_value;
    }
    void get() const noexcept
    {
        assert(has_value());
    }
    void consume() noexcept
    {
        assert(has_value());
        _has_value = false;
    }
    template<typename F>
    decltype(auto) dispatch_once(F&& f)
    {
        consume();
        return std::invoke(std::forward<F>(f));
    }
    virtual void finish() = 0;
};

template<typename T>
struct Task_Callback : Task_Storage<T>
{
    std::move_only_function<void ()> _callback;
    virtual void finish() override
    {
        if (_callback)
        {
            std::move_only_function<void ()> call = std::move(_callback);
            call();
        }
    }
};

template<typename T>
struct Task
{
    using type = T;
    Task_Ptr<Task_Callback<T>> _ptr;
    explicit Task() noexcept
        : _ptr(new(std::nothrow) Task_Callback<T>{})
    {
        assert(_ptr.get());
    }
    explicit Task(Task_Callback<T>* ptr) noexcept // explicit share
        : _ptr(ptr)
    {
        assert(ptr);
        ptr->add_ref();
    }
    ~Task() noexcept = default;
    Task(Task&& rhs) noexcept = default;
    Task& operator=(Task&& rhs) noexcept = default;
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task share()
    {
        assert(is_valid());
        return Task(_ptr.get());
    }

    void* share_address()
    {
        assert(_ptr);
        _ptr->add_ref();
        return _ptr.get();
    }

    static Task from_address(void* ptr)
    {
        assert(ptr);
        Task_Callback<T>* task_ptr = static_cast<Task_Callback<T>*>(ptr);
        Task task{task_ptr};
        task_ptr->release();
        return task;
    }

    decltype(auto) get() const
    {
        assert(is_valid());
        return _ptr->get();
    }

    decltype(auto) get_once()
    {
        assert(is_valid());
        return _ptr->consume();
    }

    template<typename... Ts>
        requires ((sizeof...(Ts)) == 0)
    void set_value(Ts&&... vs)
    {
        assert(is_valid());
        _ptr->set_value();
    }
    template<typename... Ts>
        requires ((sizeof...(Ts)) == 1)
    void set_value(Ts&&... vs)
    {
        assert(is_valid());
        (_ptr->set_value(std::forward<Ts>(vs)), ...);
    }

    bool has_value() const
    {
        assert(is_valid());
        return _ptr->has_value();
    }

    bool is_valid() const
    {
        return (_ptr.get() != nullptr);
    }

    // then() implementation
    template<typename U, typename C>
    void attach_callback(Task<U>& target, C&& callback);

    template<typename F>
    auto then(F&& f);
};

template<typename F, typename T>
struct invoke_result
{
    using type = std::invoke_result_t<std::decay_t<F>, T&&>;
};
template<typename F>
struct invoke_result<F, void>
{
    using type = std::invoke_result_t<std::decay_t<F>>;
};
template<typename F, typename T>
using invoke_result_t = typename invoke_result<F, T>::type;

template<typename T>
constexpr bool is_task_type_v = false;

template<typename U>
constexpr bool is_task_type_v<Task<U>> = true;

// std::identity<>, but handles types properly
template<typename T>
struct Identity_Callback
{
    T operator()(T v) const noexcept
    {
        return std::move(v);
    }
};

template<typename T>
struct Identity_Callback<T&>
{
    T& operator()(T& v) const noexcept
    {
        return v;
    }
};

template<>
struct Identity_Callback<void>
{
    void operator()() const noexcept
    {
    }
};

template<typename T>
template<typename U, typename F>
void Task<T>::attach_callback(Task<U>& target, F&& callback)
{
    using Result = invoke_result_t<F, T>;
    assert(is_valid());
    assert(target.is_valid());
    assert(bool(_ptr->_callback) == false);

    _ptr->_callback = [
          f = std::forward<F>(callback)
        , self_ptr = _ptr.get()
        , target = target.share()
        ]() mutable
    {
        if constexpr (std::is_same_v<void, Result>)
        {
            self_ptr->dispatch_once(std::move(f));
            target.set_value();
        }
        else if constexpr (is_task_type_v<Result> == false)
        {
            target.set_value(
                self_ptr->dispatch_once(std::move(f)));
        }
        else // unwrap inner Task
        {
            auto inner_task = self_ptr->dispatch_once(std::move(f));
            inner_task.attach_callback(target, Identity_Callback<U>{});
        }
    };

    if (_ptr->has_value())
    {
        _ptr->finish();
    }
    // else: to be invoked later, on a first call to set_value()
};

template<typename T>
template<typename F>
auto Task<T>::then(F&& f)
{
    using Return = invoke_result_t<F, T>;
    if constexpr (is_task_type_v<Return>)
    {
        Task<typename Return::type> task;
        attach_callback(task, std::forward<F>(f));
        return task;
    }
    else
    {
        Task<Return> task;
        attach_callback(task, std::forward<F>(f));
        return task;
    }
}

Task<std::string> CURL_task_get(CURL_Async curl_async, const std::string& url)
{
    Task<std::string> task;
    void* ptr = task.share_address();
    CURL_async_get(curl_async, url, ptr
        , [](void* user_data, std::string response)
    {
        Task<std::string> task = Task<std::string>::from_address(user_data);
        task.set_value(std::move(response));
    });
    return task;
}

static Task<void> Main_Task(CURL_Async curl_async)
{
    return CURL_task_get(curl_async, "localhost:5001/file1.txt")
        .then([curl_async](std::string r1)
    {
        std::println("{}", r1);
        return CURL_task_get(curl_async, "localhost:5001/file2.txt")
            .then([](std::string r2)
        {
            std::println("{}", r2);
        });
    });
}

int main()
{
    CURL_Async curl_async = CURL_async_create();
    Task<void> task = Main_Task(curl_async);
    while (task.has_value() == false)
    {
        CURL_async_tick(curl_async);
    }
    CURL_async_destroy(curl_async);
}
