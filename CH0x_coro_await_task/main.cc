#include <print>
#include <coroutine>
#include <variant>

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

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
struct Co_Task
{
    struct promise_type;
    using co_handle = std::coroutine_handle<promise_type>;

    struct promise_type : promise_return<T>
    {
        std::coroutine_handle<> _waiting_coro = std::noop_coroutine();

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
                    return self_coro.promise()._waiting_coro;
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
        return _coro;
    }

    co_handle _coro;
};

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
