#include <print>
#include <string>
#include <functional>
#include <unordered_map>
#include <utility>
#include <variant>
#include <future>
#include <type_traits>

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
        ::set_value(MOV(_r), MOV(_v));
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

// just_error('x')
template<typename Receiver, typename E>
struct State_Just_Error
{
    Receiver _r;
    E _e;
    void start() noexcept
    {
        ::set_error(MOV(_r), MOV(_e));
    }
};

template<typename E>
struct Sender_Just_Error
{
    using value_t = void_t;
    using error_t = E;
    E _e;
    template<typename Receiver>
    auto connect(Receiver&& r) noexcept
    {
        return State_Just_Error<REMOVE_CVR(Receiver), E>{._r = FWD(r), ._e = MOV(_e)};
    }
};

template<typename E>
auto just_error(E&& e) noexcept
{
    return Sender_Just_Error<REMOVE_CVR(E)>{._e = FWD(e)};
}

// just_stopped()
template<typename Receiver>
struct State_Just_Stopped
{
    Receiver _r;
    void start() noexcept
    {
        ::set_stopped(MOV(_r));
    }
};

struct Sender_Just_Stopped
{
    using value_t = void_t;
    using error_t = void_t;

    template<typename Receiver>
    auto connect(Receiver&& r) noexcept
    {
        return State_Just_Stopped<REMOVE_CVR(Receiver)>{._r = FWD(r)};
    }
};

auto just_stopped() noexcept
{
    return Sender_Just_Stopped{};
}

// sync_wait(just(1))
template<typename T, typename E>
struct sync_wait_result : std::variant<std::monostate, T, E>
{
    bool was_stopped() const { return (this->index() == 0); }
    bool has_value() const { return (this->index() == 1); }
    bool has_error() const { return (this->index() == 2); }
    template<typename U>
    void set_value(U&& v) { this->template emplace<1>(FWD(v)); }
    template<typename U>
    void set_error(U&& e) { this->template emplace<2>(FWD(e)); }
    void set_stopped() { this->template emplace<0>(); }
    T& value() { return std::get<1>(*this); }
    E& error() { return std::get<2>(*this); }
};

template<typename T, typename E>
struct State_SyncWait : sync_wait_result<T, E>
{
    std::promise<void> _done;
    sync_wait_result<T, E>& get()
    {
        return *this;
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
        _state->set_value(FWD(v));
        _state->_done.set_value();
    }
    template<typename U>
    void set_error(U&& e) noexcept
    {
        _state->set_error(FWD(e));
        _state->_done.set_value();
    }
    void set_stopped() noexcept
    {
        _state->set_stopped();
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
    return MOV(state.get());
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

// when_all()
template<typename Receiver, typename Is, typename... Senders>
struct Result_When_All;

template<typename Receiver, auto... Is, typename... Senders>
struct Result_When_All<Receiver, std::index_sequence<Is...>, Senders...>
{
    using Results = std::tuple<
        sync_wait_result<
              sender_value_t<Senders>
            , sender_error_t<Senders>
            >...
        >;
    std::mutex _lock;
    Receiver _r;
    Results _rs;
    bool _has_error = false;
    bool _was_stopped = false;
    std::int32_t _count = sizeof...(Senders);

    template<auto I, typename U>
    void set_value(U&& v) noexcept
    {
        std::lock_guard _(_lock);
        if ((_has_error || _was_stopped) == false)
        {
            std::get<I>(_rs).set_value(FWD(v));
        }
        try_finish();
    }
    template<auto I, typename U>
    void set_error(U&& e) noexcept
    {
        std::lock_guard _(_lock);
        if ((_has_error || _was_stopped) == false)
        {
            std::get<I>(_rs).set_error(FWD(e));
            _has_error = true;
            // real when_all() - also cancels the rest
        }
        try_finish();
    }
    template<auto I>
    void set_stopped() noexcept
    {
        std::lock_guard _(_lock);
        if ((_has_error || _was_stopped) == false)
        {
            std::get<I>(_rs).set_stopped();
            _was_stopped = true;
            // real when_all() - also cancels the rest
        }
        try_finish();
    }

    void try_finish()
    {
        _count -= 1;
        assert(_count >= 0);
        if (_count == 0)
        {
            finish();
        }
    }

    void finish()
    {
        if (_was_stopped)
        {
            ::set_stopped(MOV(_r));
        }
        else if (_has_error)
        {
            std::variant<sender_error_t<Senders>...> es;
            ((std::get<Is>(_rs).has_error()
                ? (void)es.template emplace<Is>(
                    MOV(std::get<Is>(_rs).error()))
                : (void)0
                ), ...);
            ::set_error(MOV(_r), MOV(es));
        }
        else
        {
            ::set_value(MOV(_r)
                , std::tuple<sender_value_t<Senders>...>(
                    std::get<Is>(_rs).value()...
                    )
                );
        }
    }
};

template<auto I, typename Results>
struct Receiver_When_All
{
    Results* _r = nullptr;

    template<typename U>
    void set_value(U&& v) noexcept
    {
        _r->template set_value<I>(FWD(v));
    }
    template<typename U>
    void set_error(U&& e) noexcept
    {
        _r->template set_error<I>(FWD(e));
    }
    void set_stopped() noexcept
    {
        _r->template set_stopped<I>();
    }
};

template<typename Sender, typename Receiver>
using operation_state_t = decltype(::connect(
    std::declval<Sender>(), std::declval<Receiver>()));

// To handle non-default-constructible states.
template<typename Sender, typename Receiver>
using state_storage_t = std::variant<std::monostate
    , operation_state_t<Sender, Receiver>>;

template<typename Receiver, typename SendersTuple, typename SendersIndexes>
struct State_When_All;

template<typename Receiver, typename... Senders, auto... Is>
struct State_When_All<Receiver
    , std::tuple<Senders...>     // Senders tuple
    , std::index_sequence<Is...> // Senders indexes
    >
{
    using Results = Result_When_All<
          Receiver
        , std::index_sequence<Is...>
        , Senders...
        >;
    using operations_tuple_t = std::tuple<
        state_storage_t<Senders
            , Receiver_When_All<Is, Results>
            >...
        >;
    using senders_tuple = std::tuple<Senders...>;

    Results _results;
    senders_tuple _ss;
    operations_tuple_t _states;

    template<auto I>
    void apply_sender()
    {
        using Receiver_ = Receiver_When_All<I, Results>;

        auto& sender = std::get<I>(_ss);
        auto& state_storage = std::get<I>(_states);
        auto& state = state_storage.template emplace<1>(
            ::connect(MOV(sender), Receiver_{._r = &_results}));
        ::start(state);
    }

    void start() noexcept
    {
        (apply_sender<Is>(), ...);
    }
};

template<typename... Senders>
struct Sender_When_All
{
    using value_t = std::tuple<sender_value_t<Senders>...>;
    using error_t = std::variant<sender_error_t<Senders>...>;

    std::tuple<Senders...> _ss;

    template<typename... Ss>
    Sender_When_All(Ss&&... ss)
        : _ss{FWD(ss)...}
    {
    }

    template<typename Receiver>
    auto connect(Receiver&& r) noexcept
    {
        using State = State_When_All<
              REMOVE_CVR(Receiver)
            , std::tuple<Senders...>
            , std::index_sequence_for<Senders...>
            >;
        return State{._results{._r = FWD(r)}, ._ss{MOV(_ss)}};
    }
};

template<typename... Senders>
auto when_all(Senders&&... ss)
{
    static_assert(sizeof...(Senders) >= 1);
    return Sender_When_All<REMOVE_CVR(Senders)...>{FWD(ss)...};
}

// sequence()
struct Receiver_Sequence
{
    using R = sync_wait_result<void_t, void_t>;
    // Should not allocate due to SBO.
    std::function<void (R)> _finish;
    template<typename U>
    void set_value(U&& v) noexcept
    {
        R r;
        r.set_value(MOV(v));
        _finish(MOV(r));
    }
    template<typename U>
    void set_error(U&& e) noexcept
    {
        R r;
        r.set_error(MOV(e));
        _finish(MOV(r));
    }
    void set_stopped() noexcept
    {
        R r;
        r.set_stopped();
        _finish(MOV(r));
    }
};

template<typename Receiver, typename SendersTuple, typename SendersIndexes>
struct State_Sequence;

template<typename Receiver, typename... Senders, auto... Is>
struct State_Sequence<Receiver
    , std::tuple<Senders...>
    , std::index_sequence<Is...>
    >
{
    // Could be a variant since one operation is active
    // at a single point in time; however, it gets complicated
    // when operation completes and destroys its own state...
    using operations_tuple_t = std::tuple<
        state_storage_t<Senders, Receiver_Sequence>...
        >;
    using senders_tuple_t = std::tuple<Senders...>;

    Receiver _r;
    senders_tuple_t _ss;
    operations_tuple_t _states;

    template<auto I>
    void apply_sender()
    {
        auto apply_next = [this](sync_wait_result<void_t, void_t> v)
        {
            if (v.has_error())
            {
                ::set_error(MOV(_r), MOV(v.error()));
            }
            else if (v.was_stopped())
            {
                ::set_stopped(MOV(_r));
            }
            else if constexpr ((I + 1) >= sizeof...(Senders))
            {
                ::set_value(MOV(_r), MOV(v.value()));
            }
            else
            {
                apply_sender<I + 1>();
            }
        };

        auto& sender = std::get<I>(_ss);
        auto& state_storage = std::get<I>(_states);
        auto& state = state_storage.template emplace<1>(
            ::connect(MOV(sender)
                , Receiver_Sequence{._finish = MOV(apply_next)}));
        ::start(state);
    }

    void start() noexcept
    {
        apply_sender<0>();
    }
};

template<typename... Senders>
struct Sender_Sequence
{
    using value_t = void_t;
    using error_t = void_t;

    std::tuple<Senders...> _ss;

    template<typename... Ss>
    Sender_Sequence(Ss&&... ss)
        : _ss{FWD(ss)...}
    {
    }

    template<typename Receiver>
    auto connect(Receiver&& r) noexcept
    {
        using State = State_Sequence<
              REMOVE_CVR(Receiver)
            , std::tuple<Senders...>
            , std::index_sequence_for<Senders...>
            >;
        return State{._r = FWD(r), ._ss = MOV(_ss)};
    }
};

template<typename... Senders>
auto sequence(Senders&&... ss)
{
    static_assert(sizeof...(Senders) >= 1);
    static_assert(std::conjunction_v<std::is_same<void_t, sender_value_t<Senders>>...>
        , "we expect all of the Senders to return void on success");
    static_assert(std::conjunction_v<std::is_same<void_t, sender_error_t<Senders>>...>
        , "we expect all of the Senders to return void on error");
    return Sender_Sequence<REMOVE_CVR(Senders)...>{FWD(ss)...};
}

template<typename Receiver>
struct State_CURL_Get
{
    void start() noexcept
    {
        assert(_curl_async);
        CURL_async_get(_curl_async, _url
            , this
            , [](void* user_data, std::string response)
        {
            State_CURL_Get& state = *static_cast<State_CURL_Get*>(user_data);
            ::set_value(MOV(state._receiver), MOV(response));
        });
    }

    Receiver _receiver;
    CURL_Async _curl_async = nullptr;
    std::string _url;
};

struct Sender_CURL_Get
{
    using value_t = std::string;
    using error_t = void_t;

    CURL_Async _curl_async = nullptr;
    std::string _url;

    template<typename Receiver>
    auto connect(Receiver&& r)
    {
        return State_CURL_Get<REMOVE_CVR(Receiver)>
        {
            ._receiver = FWD(r),
            ._curl_async = _curl_async,
            ._url = std::move(_url)
        };
    }
};

Sender_CURL_Get CURL_sender_get(CURL_Async curl_async, const std::string& url)
{
    return Sender_CURL_Get
    {
        ._curl_async = curl_async,
        ._url = url
    };
}

auto App_SendersV0(CURL_Async curl_async)
{
    return sequence(
        then(CURL_sender_get(curl_async, "localhost:5001/file1.txt")
            , [](std::string r1) -> void_t
        {
            std::println("{}", r1);
            return {};
        }),
        then(CURL_sender_get(curl_async, "localhost:5001/file2.txt")
            , [](std::string r2) -> void_t
        {
            std::println("{}", r2);
            return {};
        })
        );
}

auto App_SendersV1(CURL_Async curl_async)
{
    return then(
        when_all(
              CURL_sender_get(curl_async, "localhost:5001/file1.txt")
            , CURL_sender_get(curl_async, "localhost:5001/file2.txt")
            )
        , [](auto vs) -> void_t
        {
            auto [r1, r2] = vs;
            std::println("{}", r1);
            std::println("{}", r2);
            return void_t{};
        });
}

struct AnyReceiver
{
    template<typename T>
    void set_value(T&&...) noexcept { finish(); }
    template<typename E>
    void set_error(E&&) noexcept    { finish(); }
    void set_stopped() noexcept     { finish(); }

    void finish() noexcept
    {
        assert(_done);
        assert(*_done == false);
        *_done = true;
    }

    bool* _done = nullptr;
};

int main()
{
    CURL_Async curl_async = CURL_async_create();

    bool done1 = false;
    auto state1 = ::connect(App_SendersV0(curl_async), AnyReceiver{&done1});
    ::start(state1);

    bool done2 = false;
    auto state2 = ::connect(App_SendersV1(curl_async), AnyReceiver{&done2});
    ::start(state2);

    while ((done1 == false)
        || (done2 == false))
    {
        CURL_async_tick(curl_async);
    }
    CURL_async_destroy(curl_async);
}
