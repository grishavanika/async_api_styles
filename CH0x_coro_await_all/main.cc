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

template<typename T>
struct S;

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
