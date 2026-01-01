export module generator;

import <optional>;
import <coroutine>;
import <vector>;

#define CORO_INLINE __declspec(noinline)

struct Placeholder {};

export template<typename BackType = void>
struct TGenPromiseBase
{
    std::optional<BackType> back_value;
};

export template<>
struct TGenPromiseBase<void>
{
};

export template <typename HandleType, typename Y>
struct TGeneratorAwaiter
{
    HandleType handle;

    bool await_ready()
    {
        return false;
    }

    template <typename P>
    void await_suspend(std::coroutine_handle<P> Continuation)
    {
        handle = Continuation;
    }

    auto await_resume()
    {
        auto& Promise = handle.promise();
        return static_cast<TGenPromiseBase<Y>>(Promise).back_value.GetValue();
    }
};

export template <typename HandleType>
struct TGeneratorAwaiter<HandleType, void>
{
    bool await_ready()
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> Continuation)
    {
    }

    auto await_resume()
    {
    }
};

export template<typename YieldType = Placeholder, typename ConsumeType = void>
struct Generator
{
    struct promise_type;
    using HandleType = std::coroutine_handle<promise_type>;


    struct promise_type : public TGenPromiseBase<ConsumeType>
    {
        CORO_INLINE auto get_return_object()
        {
            return HandleType::from_promise(*this);
        }

        std::optional<YieldType> current_value;
        bool has_done = false;

        auto initial_suspend() noexcept
        {
            return std::suspend_always{};
        }

        auto final_suspend() noexcept
        {
            return std::suspend_always();
        }

			CORO_INLINE void unhandled_exception()
        {
        }

			CORO_INLINE void return_void()
        {
            has_done = true;
        }

        template <typename U = YieldType>
        CORO_INLINE auto yield_value(U&& NextValue)
        {
            current_value.emplace(std::forward<U>(NextValue));
            return TGeneratorAwaiter<HandleType, ConsumeType>{};
        }
    };

    Generator(HandleType CoroHandle)
        : has_done(false)
        , handle(CoroHandle)
    {
    }


    Generator(Generator&& other)
    {
        has_done = other.has_done;
        handle = other.handle;
        other.handle = nullptr;
    }


    Generator(const Generator& other)
    {
        has_done = other.has_done;
        handle = other.handle;
    }

    auto operator=(const Generator& other)
    {
        has_done = other.has_done;
        handle = other.handle;
        return *this;
    }

    ~Generator()
    {
        if (handle && !has_done)
            handle.destroy();
    }

    struct Iterator
    {
        Generator<YieldType>* generator;

        Iterator(Generator<YieldType>* in_generator)
            : generator(in_generator)
        {
        }

        CORO_INLINE operator bool()
        {
            if (generator)
            {
                return !generator->handle.promise().has_done;
            }
            return false;
        }

        CORO_INLINE YieldType* operator->()
        {
            return &generator->handle.promise().current_value.value();
        }

			CORO_INLINE YieldType& operator*()
        {
            return generator->handle.promise().current_value.value();
        }

			CORO_INLINE Iterator& operator++()
        {
            generator->handle.resume();
            return *this;
        }
    };

		CORO_INLINE YieldType& Next()
    {
        handle.resume();
        return value();
    }

    template <typename U>
    CORO_INLINE typename std::enable_if_t<!std::is_same_v<U, void>, YieldType&> send(U&& AwaiterValue)
    {
        static_cast<TGenPromiseBase<ConsumeType>&>(handle.promise()).back_value = AwaiterValue;
        handle.resume();
        return value();
    }

		CORO_INLINE YieldType& value()
    {
        if (handle.promise().current_value.IsSet())
        {
            return handle.promise().current_value.GetValue();
        }
        static YieldType DefaultValue{};
        return DefaultValue;
    }

		CORO_INLINE Iterator begin()
    {
        handle.resume();
        return Iterator(this);
    }

    CORO_INLINE auto end()
    {
        return Iterator(nullptr);
    }

    operator std::vector<YieldType>()
    {
        std::vector<YieldType> Result;
        for (auto& value : *this)
            Result.push_back(value);
        return Result;
    }

private:
    bool has_done;
    HandleType handle;
};


template <typename T>
inline T next(Generator<T>& Generator)
{
    return Generator.Next();
}

template <typename T, typename Y>
inline T next(Generator<T, Y>& Generator, Y&& value)
{
    return Generator.send(value);
}


#define CO_BORROW_COMBINE_INNER(A,B) A##B
#define CO_BORROW_COMBINE(A,B) CO_BORROW_COMBINE_INNER(A,B)

#define CO_BORROW(Source, Dest) \
	using CO_BORROW_COMBINE(BorrowedType_,__LINE__) = std::conditional_t<\
		std::is_rvalue_reference_v<decltype(Source)>,\
		std::remove_reference_t<decltype(Source)>,\
		std::remove_reference_t<decltype(Source)>&>;\
	CO_BORROW_COMBINE(BorrowedType_,__LINE__) Dest = Source;
