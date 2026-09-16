#include <utility>
#include <variant>
#include <future>
#include <type_traits>

#include <cassert>

#define FWD(...) ::std::forward<decltype(__VA_ARGS__)>(__VA_ARGS__)
#define MOV(...) ::std::move(__VA_ARGS__)
#define REMOVE_CVR(...) std::remove_cvref_t<__VA_ARGS__>
using void_t = std::monostate;

// Receiver.
template<typename Receiver, typename T>
void set_value(Receiver&& r, T&& v) noexcept
{
    FWD(r).set_value(FWD(v));
}

template<typename Receiver, typename E>
void set_error(Receiver&& r, E&& e) noexcept
{
    FWD(r).set_error(FWD(e));
}

template<typename Receiver>
void set_stopped(Receiver&& r) noexcept
{
    FWD(r).set_stopped();
}

// Sender.
template<typename Sender, typename Receiver>
auto connect(Sender&& s, Receiver&& r) noexcept
{
    return FWD(s).connect(FWD(r));
}

template<typename Sender>
using sender_value_t = typename REMOVE_CVR(Sender)::value_t;

template<typename Sender>
using sender_error_t = typename REMOVE_CVR(Sender)::error_t;

// Operation.
template<typename Operation>
void start(Operation& o) noexcept
{
    o.start();
}

// just(1)
template<typename Receiver, typename T>
struct State_Just
{
    Receiver _r;
    T _v;
    void start() noexcept
    {
        set_value(MOV(_r), MOV(_v));
    }
};

template<typename T>
struct Sender_Just
{
    using value_t = T;
    using error_t = void_t;
    T _v;
    template<typename Receiver>
    auto connect(Receiver&& r) noexcept
    {
        return State_Just<REMOVE_CVR(Receiver), T>{._r = FWD(r), ._v = MOV(_v)};
    }
};

template<typename T>
auto just(T&& v) noexcept
{
    return Sender_Just<REMOVE_CVR(T)>{._v = FWD(v)};
}

// sync_wait(just(1))
template<typename T, typename E>
struct sync_wait_result : std::variant<std::monostate, T, E>
{
    bool was_stopped() const { return (this->index() == 0); }
    bool has_value() const { return (this->index() == 1); }
    bool has_error() const { return (this->index() == 2); }
    T& value() { return std::get<1>(*this); }
    E& error() { return std::get<2>(*this); }
};

template<typename T, typename E>
struct State_SyncWait : sync_wait_result<T, E>
{
    std::promise<void> _done;
    auto get_once()
    {
        return sync_wait_result<T, E>{std::move(*this)};
    }
    void wait()
    {
        _done.get_future().wait();
    }
};

template<typename T, typename E>
struct Receiver_SyncWait
{
    State_SyncWait<T, E>* _state = nullptr;
    template<typename U>
    void set_value(U&& v) noexcept
    {
        _state->template emplace<1>(FWD(v));
        _state->_done.set_value();
    }
    template<typename U>
    void set_error(U&& e) noexcept
    {
        _state->template emplace<2>(FWD(e));
        _state->_done.set_value();
    }
    void set_stopped() noexcept
    {
        _state->template emplace<0>();
        _state->_done.set_value();
    }
};

template<typename Sender>
auto sync_wait(Sender&& s) noexcept
{
    using T = sender_value_t<Sender>;
    using E = sender_error_t<Sender>;
    State_SyncWait<T, E> state;
    auto o = connect(FWD(s), Receiver_SyncWait<T, E>{._state = &state});
    start(o);
    state.wait();
    return state.get_once();
}

// then(just(1), [](int v) { return ...; })
template<typename Receiver, typename Lambda>
struct Receiver_Then
{
    Receiver _r;
    Lambda _f;
    template<typename U>
    void set_value(U&& v) noexcept
    {
        ::set_value(MOV(_r), MOV(_f)(FWD(v)));
    }
    template<typename U>
    void set_error(U&& e) noexcept
    {
        ::set_error(MOV(_r), FWD(e));
    }
    void set_stopped() noexcept
    {
        ::set_stopped(MOV(_r));
    }
};

template<typename Sender, typename Lambda>
struct Sender_Then
{
    using inner_value_t = sender_value_t<Sender>;
    using value_t = std::invoke_result_t<Lambda, inner_value_t>;
    using error_t = sender_error_t<Sender>;
    Sender _s;
    Lambda _f;
    template<typename Receiver>
    auto connect(Receiver&& r) noexcept
    {
        using Receiver_ = Receiver_Then<REMOVE_CVR(Receiver), Lambda>;
        return ::connect(MOV(_s), Receiver_{._r = FWD(r), ._f = MOV(_f)});
    }
};

template<typename Sender, typename Lambda>
auto then(Sender&& s, Lambda&& f) noexcept
{
    return Sender_Then<REMOVE_CVR(Sender), REMOVE_CVR(Lambda)>{._s = FWD(s), ._f = FWD(f)};
}

// async()
template<typename Receiver>
struct State_Async
{
    Receiver _r;
    std::future<void> _f;
    void start() noexcept
    {
        _f = std::async(std::launch::async
            , [r = MOV(_r)]() mutable
        {
            ::set_value(MOV(r), void_t());
        });
    }
};

struct Sender_Async
{
    using value_t = void_t;
    using error_t = void_t;
    template<typename Receiver>
    auto connect(Receiver&& r) noexcept
    {
        return State_Async<REMOVE_CVR(Receiver)>{._r = FWD(r)};
    }
};

auto async()
{
    return Sender_Async{};
}

int main()
{
    const auto main_thread_id = std::this_thread::get_id();
    std::thread::id work_thread_id;
    auto x = then(async()
        , [&](void_t) -> void_t
    {
        work_thread_id = std::this_thread::get_id();
        return {};
    });
    auto r = sync_wait(MOV(x));
    assert(r.has_value());
    assert(main_thread_id != work_thread_id);
}
