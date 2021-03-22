#pragma once
#include <type_traits>
#include <exception>

template <typename F>
struct function;

template <typename T>
constexpr bool is_copy_v = std::is_nothrow_copy_constructible_v<T>;

template <typename T>
constexpr bool is_small_v = sizeof(T) <= sizeof(void*)
                            && (alignof(void*) % alignof(T) == 0);  // ?? check if T is_nothrow_move_constructible

struct bad_function_call : std::exception {
    char const* what() const noexcept override {
        return "empty function call";
    }
};

template <typename R, typename... Args>
struct methods {
    using invoke_fn_t = R (*)(void*, Args...);
    using free_fn_t = void (*)(void*);
    using copy_fn_t = void* (*)(void*);

    invoke_fn_t invoke;
    free_fn_t free;
    copy_fn_t copy;
};

template <typename R, typename... Args>
struct storage {
    void* obj;

    methods<R, Args...> const* methods_table;
};

template <typename R, typename... Args>
methods<R, Args...> const* get_empty_methods() {
    static constexpr methods<R, Args...> table {
            [] (void*, Args...) -> R { // invoke
                throw bad_function_call();
            },
            [] (void*) { // free
                return;
            },
            [] (void*) -> void* { // copy
                return nullptr;
            }
    };

    return &table;
}

template <typename T, bool isSmall>
struct object_traits;

template <typename T>
struct object_traits<T, false> {
    template <typename R, typename... Args>
    static methods<R, Args...> const* get_methods() {
        static constexpr methods<R, Args...> table {
                [] (void* obj, Args... args) -> R {
                    return (*static_cast<T*>(obj))(std::forward<Args>(args)...);
                },
                [] (void* obj) {
                    delete static_cast<T*>(obj);
                },
                [] (void* obj) -> void* {
                    throw std::exception();
                }
        };

        return &table;
    }
};

template <typename T>
struct object_traits<T, true> {
    template <typename R, typename... Args>
    static methods<R, Args...> const* get_methods() {
        static constexpr methods<R, Args...> table {
                [] (void* obj, Args... args) -> R {
                    return (*static_cast<T*>(obj))(std::forward<Args>(args)...);
                },
                [] (void* obj) {
                    delete static_cast<T*>(obj);
                },
                [] (void* obj) -> void* {
                    return new T(*static_cast<T*>(obj));
                }
        };

        return &table;
    }
};



template <typename R, typename... Args>
struct function<R (Args...)>
{
    function() noexcept {
        stg.obj = nullptr;
        stg.methods_table = get_empty_methods<R, Args...>();
    }

    template<typename T>
    function(T val) {
        // T small ???
        stg.obj = new T(std::move(val));
        stg.methods_table = object_traits<T, is_copy_v<T>>::template get_methods<R, Args...>();
    }

    function(function const& other) {
        stg.methods_table = other.stg.methods_table;
        stg.obj = other.stg.methods_table->copy(other.stg.obj);
    }

    function(function&& other) noexcept {
        if (&other == this)
            return;
        stg.obj = other.stg.obj;
        stg.methods_table = other.stg.methods_table;
        other.stg.obj = nullptr;
        other.stg.methods_table = get_empty_methods<R, Args...>();
    }

    function& operator=(function const& rhs) {
        if (&rhs != this) {
            stg.obj = rhs.stg.methods_table->copy(rhs.stg.obj);
            stg.methods_table = rhs.stg.methods_table;
        }
        return *this;
    }

    function& operator=(function&& rhs) noexcept {
        if (&rhs != this) {
            stg.obj = std::move(rhs.stg.obj);
            stg.methods_table = rhs.stg.methods_table;
            rhs.stg.obj = nullptr;
            rhs.stg.methods_table = get_empty_methods<R, Args...>();
        }
        return *this;
    }

    ~function() {
        stg.methods_table->free(stg.obj);
    }

    explicit operator bool() const noexcept {
        return stg.methods_table != get_empty_methods<R, Args...>();
    }

    R operator()(Args... args) const {
        return stg.methods_table->invoke(stg.obj, std::forward<Args>(args)...);
    }

    template <typename T>
    T* target() noexcept {
        if (stg.methods_table != object_traits<T, true>::template get_methods<R, Args...>())
            return nullptr;
        else
            return static_cast<T*>(stg.obj);
    }

    template <typename T>
    T const* target() const noexcept {
        if (stg.methods_table != object_traits<T, true>::template get_methods<R, Args...>())
            return nullptr;
        else
            return static_cast<T const*>(stg.obj);
    }

private:
    storage<R, Args...> stg;
};
