#include <print>
#include <exception>
#include <variant>
#include <vector>
#include <type_traits>
#include <string>
#include <functional>
#include <unordered_map>
#include <optional>

#include <curl/curl.h>

#include <Windows.h>
// Note: the floating-point state on x86 systems is not preserved.
// If there is need to support x86, Fiber's Ex-tended API must be used.
#if !defined(_WIN64)
#  error Fiber implementation does not support x86 systems.
#endif
static_assert(std::is_same_v<LPVOID, void*>);

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
    status = curl_easy_setopt(curl_easy
        , CURLOPT_WRITEFUNCTION, CURL_OnWriteCallback);
    assert(status == CURLE_OK);
    status = curl_easy_setopt(curl_easy, CURLOPT_WRITEDATA, state);
    assert(status == CURLE_OK);

    // 3. associate with multi handle/event loop
    CURL_scheduler(curl_async).add_request(curl_easy
        , [state, user_data, callback](CURL* curl_easy_)
    {
        long response_code = -1;
        const CURLcode status_ = curl_easy_getinfo(curl_easy_
            , CURLINFO_RESPONSE_CODE, &response_code);
        assert(status_ == CURLE_OK);
        assert(response_code == 200L && "RUN serve.cmd");
        curl_easy_cleanup(curl_easy_);
        std::string data = std::move(*state);
        delete state;
        callback(user_data, std::move(data));
    });
}

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

struct Fiber
{
    void* _fiber = nullptr;
    void* _parent_fiber = nullptr;
    FiberCallbackBase* _callback = nullptr;
    std::exception_ptr _exception;

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

    void suspend()
    {
        assert(_parent_fiber);
        void* switch_to_fiber = _parent_fiber;
        _parent_fiber = nullptr;
        ::SwitchToFiber(switch_to_fiber);
        assert(_callback);
        _callback->resume();
    }

    void resume()
    {
        assert(_parent_fiber == nullptr);
        assert(_callback);
        _parent_fiber = ::GetCurrentFiber();
        ::SwitchToFiber(_fiber);
    }

    void run()
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

    void set_callback(FiberCallbackBase& callback)
    {
        assert(_callback == nullptr);
        _callback = &callback;
        _exception = {};
    }

    bool is_busy() const
    {
        if (_exception)
        {
            std::rethrow_exception(_exception);
        }
        return !!_callback;
    }

    struct Boot
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
};

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

struct FiberPool
{
    using Handle = std::size_t;

    explicit FiberPool(std::size_t size) noexcept
    {
        assert(size > 0);
        _fibers.reset(new(std::nothrow) Fiber[size]);
        assert(_fibers.get());
        _size = size;
    }

    Handle allocate(FiberCallbackBase& callback)
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

    void free(Handle handle)
    {
        assert(handle < _size);
        assert(_fibers[handle]._callback == nullptr);
    }

    void suspend(Handle handle)
    {
        assert(handle < _size);
        _fibers[handle].suspend();
    }

    void resume(Handle handle)
    {
        assert(handle < _size);
        _fibers[handle].resume();
    }

    bool is_busy(Handle handle) const
    {
        assert(handle < _size);
        return _fibers[handle].is_busy();
    }

private:
    std::unique_ptr<Fiber[]> _fibers;
    std::size_t _size = 0;
};

class Exception_FiberTaskCancelled : public std::exception
{
};

struct FiberTask_Any : public FiberCallbackBase
{
    explicit FiberTask_Any(FiberPool& fiber_pool)
        : _fiber_pool(fiber_pool)
        , _fiber(fiber_pool.allocate(*this))
    {
    }
    virtual ~FiberTask_Any() noexcept
    {
        assert(is_completed());
        _fiber_pool.free(_fiber);
    }

    bool is_completed() const
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
    void execute()
    {
        assert(is_completed() == false);
        _fiber_pool.resume(_fiber);
    }
    void cancel()
    {
        assert(_cancelled == false);
        _cancelled = true;
    }

private:
    virtual void do_resume() override
    {
        if (_cancelled)
        {
            _cancelled = false;
            throw Exception_FiberTaskCancelled{};
        }
    }

protected:
    FiberPool& _fiber_pool;
    FiberPool::Handle _fiber;
    bool _cancelled = false;
};

template<typename R>
struct FiberTask_WithResult : public FiberTask_Any
{
public:
    using FiberTask_Any::FiberTask_Any;
    const R& get() const
    {
        // Populate exception, if any.
        const bool running = _fiber_pool.is_busy(_fiber);
        assert(running == false);
        assert(_storage.index() == 1);
        return std::get<1>(_storage);
    }
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
    void get() const
    {
        // Populate exception, if any.
        const bool running = _fiber_pool.is_busy(_fiber);
        assert(running == false);
    }
    void get_once()
    {
        return static_cast<const FiberTask_WithResult&>(*this).get();
    }
};

template<typename R, typename C, typename... Args>
struct FiberTask_Callable : public FiberTask_WithResult<R>
{
    using Base = FiberTask_WithResult<R>;
public:
    explicit FiberTask_Callable(C&& callable, FiberPool& fiber_pool, Args&&... args)
        : Base(fiber_pool)
        , _args(std::move(args)...)
        , _callable(std::move(callable))
    {
    }
private:
    virtual void do_run() override
    {
        if constexpr (std::is_same_v<void, R>)
        {
            std::apply(std::move(_callable), std::move(_args));
        }
        else
        {
            this->_storage.template emplace<1>(
                std::apply(std::move(_callable), std::move(_args)));
        }
    }
private:
    std::tuple<Args...> _args;
    C _callable;
};

struct FiberTaskScheduler
{
    FiberPool& _fiber_pool;
    std::vector<std::unique_ptr<FiberTask_Any>> _tasks;
    std::vector<FiberTask_Any*> _tasks_to_remove;

    explicit FiberTaskScheduler(FiberPool& fiber_pool) noexcept
        : _fiber_pool(fiber_pool)
    {
    }
    FiberTaskScheduler(const FiberTaskScheduler&) = delete;
    ~FiberTaskScheduler() noexcept = default;

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
    void schedule()
    {
        while (schedule_once()) {}
    }
    bool schedule_once()
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
};

template<typename R>
struct FiberTask
{
    using Task = FiberTask_WithResult<R>;

    template<typename C, typename... Args>
    explicit FiberTask(C&& callable, FiberTaskScheduler& scheduler, Args... args) noexcept
        : _scheduler(&scheduler)
    {
        using TaskCallable = FiberTask_Callable<R, std::remove_cvref_t<C>, Args...>;
        TaskCallable* task = new(std::nothrow) TaskCallable(
            std::forward<C>(callable), scheduler._fiber_pool, std::move(args)...);
        assert(task);
        scheduler.add_fiber_task(std::unique_ptr<FiberTask_Any>(task));
        _task = task;
    }
    ~FiberTask() noexcept
    {
        destroy_once();
    }

    FiberTask(FiberTask&& rhs) noexcept
    {
        swap(rhs);
    }
    FiberTask& operator=(FiberTask&& rhs) noexcept
    {
        if (this != &rhs)
        {
            destroy_once();
            swap(rhs);
        }
        return *this;
    }

    FiberTask(const FiberTask&) = delete;
    FiberTask& operator=(const FiberTask&) = delete;

    decltype(auto) get() const
    {
        assert(_task);
        return _task->get();
    }
    decltype(auto) get_once()
    {
        assert(_task);
        return _task->get_once();
    }
    bool is_completed() const
    {
        assert(_task);
        return  _task->is_completed();
    }

private:
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
    void swap(FiberTask& rhs) noexcept
    {
        std::swap(_scheduler, rhs._scheduler);
        std::swap(_task, rhs._task);
    }
private:
    FiberTaskScheduler* _scheduler = nullptr;
    Task* _task = nullptr;
};

template<typename C, typename... Args>
auto FF_async(FiberTaskScheduler& scheduler, C&& callable, Args... args)
{
    using Task = FiberTask<std::invoke_result_t<C, Args...>>;
    return Task{std::forward<C>(callable), scheduler, std::move(args)...};
}

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

///////////////////////////////////////////////////////////
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

int main()
{
    App_FibersV0();
    App_FibersV1();
}
