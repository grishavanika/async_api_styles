#include <print>
#include <variant>
#include <new>
#include <functional>
#include <cstdint>

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

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
    std::unique_ptr<Task_Callback<T>, Task_Release> _ptr;
    explicit Task() noexcept
        : Task(new(std::nothrow) Task_Callback<T>{})
    {
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
            if constexpr (std::is_same_v<void, U>)
            {
                inner_task.attach_callback(target, []() {});
            }
            else
            {
                inner_task.attach_callback(target, std::identity{});
            }
        }
    };

    if (_ptr->has_value())
    {
        _ptr->finish();
    }
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

int main()
{
}
