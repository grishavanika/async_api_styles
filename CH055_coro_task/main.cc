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

    co_handle _coro;
};

static Co_Task<int> coro_main()
{
    co_return 3;
}

int main()
{
    Co_Task<int> task = coro_main();
    task.resume();
    std::println("{}", task.get_once());
}
