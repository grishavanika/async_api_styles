#include <Windows.h>
// Note: the floating-point state on x86 systems is not preserved.
// If there is need to support x86, Fiber's Ex-tended API must be used.
#if !defined(_WIN64)
#  error Fiber implementation does not support x86 systems.
#endif
#include <type_traits>
static_assert(std::is_same_v<LPVOID, void*>);

#include <print>
#include <exception>
#include <variant>
#include <vector>

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

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
            auto it_remove = std::find(_tasks_to_remove.begin(), _tasks_to_remove.end(), task.get());
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

    template<typename C>
    explicit FiberTask(C&& callable, FiberTaskScheduler& scheduler) noexcept
        : _scheduler(&scheduler)
    {
        using TaskCallable = FiberTask_Callable<R, std::remove_cvref_t<C>>;
        TaskCallable* task = new(std::nothrow) TaskCallable(std::forward<C>(callable), scheduler._fiber_pool);
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

template<typename C>
auto FF_async(FiberTaskScheduler& scheduler, C&& callable)
{
    using Task = FiberTask<std::invoke_result_t<C>>;
    return Task{std::forward<C>(callable), scheduler};
}

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
    std::println("{}", task.get());
}
