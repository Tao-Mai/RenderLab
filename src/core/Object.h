/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#pragma once

#include "core/Macros.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

namespace Falcor
{
class PluginManager;

class FALCOR_API Object
{
public:
    /// Default constructor.
    Object() = default;

    /// Copy constructor.
    /// Note: We don't copy the reference counter, so that the new object
    /// starts with a reference count of 0.
    Object(const Object&) {}

    /// Copy assignment.
    /// Note: We don't copy the reference counter, but leave the reference
    /// counts of the two objects unchanged. This results in the same semantics
    /// that we would get if we used `std::shared_ptr` where the reference
    /// counter is stored in a separate place from the object.
    Object& operator=(const Object&) { return *this; }

    /// Make the object non-movable.
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;

    /// Destructor.
    virtual ~Object() = default;

    /// Return the name of the class.
    /// Note: This reports the actual class name if FALCOR_OBJECT() is used.
    virtual const char* getClassName() const { return "Object"; }

    /// Return the current reference count.
    int refCount() const { return static_cast<int>(mRefCount.load(std::memory_order_relaxed)); }

    /// Increase the object's reference count by one.
    void incRef() const;

    /// Decrease the reference count of the object and possibly deallocate it.
    void decRef(bool dealloc = true) const noexcept;

private:
    mutable std::atomic<uint32_t> mRefCount{0};
    std::shared_ptr<void> mPluginLibrary;

    friend class PluginManager;
};

/// Macro to declare the object class name.
#define FALCOR_OBJECT(class_)                 \
public:                                       \
    const char* getClassName() const override \
    {                                         \
        return #class_;                       \
    }

/**
 * @brief Reference counting helper.
 *
 * The @a ref template is a simple wrapper to store a pointer to an object. It
 * takes care of increasing and decreasing the object's reference count as
 * needed. When the last reference goes out of scope, the associated object
 * will be deallocated.
 *
 * This class follows similar semantics to the ``std::shared_ptr`` class from
 * the STL. In particular, we avoid implicit conversion to and from raw
 * pointers.
 */
template<typename T>
class ref
{
public:
    /// Default constructor (nullptr).
    ref() {}

    /// Construct a reference from a nullptr.
    ref(std::nullptr_t) {}

    /// Construct a reference from a convertible pointer.
    template<typename T2 = T>
    explicit ref(T2* ptr) : mPtr(ptr)
    {
        static_assert(std::is_base_of_v<Object, T2>, "Cannot create reference to object not inheriting from Object class.");
        static_assert(std::is_convertible_v<T2*, T*>, "Cannot create reference to object from unconvertible pointer type.");
        if (mPtr)
            incRef((const Object*)(mPtr));
    }

    /// Copy constructor.
    ref(const ref& r) : mPtr(r.mPtr)
    {
        if (mPtr)
            incRef((const Object*)(mPtr));
    }

    /// Construct a reference from a convertible reference.
    template<typename T2 = T>
    ref(const ref<T2>& r) : mPtr(r.mPtr)
    {
        static_assert(std::is_base_of_v<Object, T>, "Cannot create reference to object not inheriting from Object class.");
        static_assert(std::is_convertible_v<T2*, T*>, "Cannot create reference to object from unconvertible reference.");
        if (mPtr)
            incRef((const Object*)(mPtr));
    }

    /// Move constructor.
    ref(ref&& r) noexcept
        : mPtr(r.mPtr)
    {
        r.mPtr = nullptr;
    }

    /// Construct a reference by moving from a convertible reference.
    template<typename T2>
    ref(ref<T2>&& r) noexcept
        : mPtr(r.mPtr)
    {
        static_assert(std::is_base_of_v<Object, T>, "Cannot create reference to object not inheriting from Object class.");
        static_assert(std::is_convertible_v<T2*, T*>, "Cannot create reference to object from unconvertible reference.");
        r.mPtr = nullptr;
    }

    /// Destructor.
    ~ref()
    {
        if (mPtr)
            decRef((const Object*)(mPtr));
    }

    /// Assign another reference into the current one.
    ref& operator=(const ref& r) noexcept
    {
        if (r != *this)
        {
            if (r.mPtr)
                incRef((const Object*)(r.mPtr));
            T* prevPtr = mPtr;
            mPtr = r.mPtr;
            if (prevPtr)
                decRef((const Object*)(prevPtr));
        }
        return *this;
    }

    /// Assign another convertible reference into the current one.
    template<typename T2>
    ref& operator=(const ref<T2>& r) noexcept
    {
        static_assert(std::is_convertible_v<T2*, T*>, "Cannot assign reference to object from unconvertible reference.");
        if (r != *this)
        {
            if (r.mPtr)
                incRef((const Object*)(r.mPtr));
            T* prevPtr = mPtr;
            mPtr = r.mPtr;
            if (prevPtr)
                decRef((const Object*)(prevPtr));
        }
        return *this;
    }

    /// Move another reference into the current one.
    ref& operator=(ref&& r) noexcept
    {
        if (static_cast<void*>(&r) != this)
        {
            T* prevPtr = std::exchange(mPtr, std::exchange(r.mPtr, nullptr));
            if (prevPtr)
                decRef((const Object*)(prevPtr));
        }
        return *this;
    }

    /// Move another convertible reference into the current one.
    template<typename T2>
    ref& operator=(ref<T2>&& r) noexcept
    {
        static_assert(std::is_convertible_v<T2*, T*>, "Cannot move reference to object from unconvertible reference.");
        if (static_cast<void*>(&r) != this)
        {
            T* prevPtr = std::exchange(mPtr, std::exchange(r.mPtr, nullptr));
            if (prevPtr)
                decRef((const Object*)(prevPtr));
        }
        return *this;
    }

    /// Overwrite this reference with a pointer to another object
    template<typename T2 = T>
    void reset(T2* ptr = nullptr) noexcept
    {
        static_assert(std::is_convertible_v<T2*, T*>, "Cannot assign reference to object from unconvertible pointer.");
        if (ptr != mPtr)
        {
            if (ptr)
                incRef((const Object*)(ptr));
            T* prevPtr = mPtr;
            mPtr = ptr;
            if (prevPtr)
                decRef((const Object*)(prevPtr));
        }
    }

    /// Compare this reference to another reference.
    template<typename T2 = T>
    bool operator==(const ref<T2>& r) const
    {
        static_assert(
            std::is_convertible_v<T2*, T*> || std::is_convertible_v<T*, T2*>, "Cannot compare references of non-convertible types."
        );
        return mPtr == r.mPtr;
    }

    /// Compare this reference to another reference.
    template<typename T2 = T>
    bool operator!=(const ref<T2>& r) const
    {
        static_assert(
            std::is_convertible_v<T2*, T*> || std::is_convertible_v<T*, T2*>, "Cannot compare references of non-convertible types."
        );
        return mPtr != r.mPtr;
    }

    /// Compare this reference to another reference.
    template<typename T2 = T>
    bool operator<(const ref<T2>& r) const
    {
        static_assert(
            std::is_convertible_v<T2*, T*> || std::is_convertible_v<T*, T2*>, "Cannot compare references of non-convertible types."
        );
        return mPtr < r.mPtr;
    }

    /// Compare this reference to a pointer.
    template<typename T2 = T>
    bool operator==(const T2* ptr) const
    {
        static_assert(std::is_convertible_v<T2*, T*>, "Cannot compare reference to pointer of non-convertible types.");
        return mPtr == ptr;
    }

    /// Compare this reference to a pointer.
    template<typename T2 = T>
    bool operator!=(const T2* ptr) const
    {
        static_assert(std::is_convertible_v<T2*, T*>, "Cannot compare reference to pointer of non-convertible types.");
        return mPtr != ptr;
    }

    /// Compare this reference to a null pointer.
    bool operator==(std::nullptr_t) const { return mPtr == nullptr; }

    /// Compare this reference to a null pointer.
    bool operator!=(std::nullptr_t) const { return mPtr != nullptr; }

    /// Compare this reference to a null pointer.
    bool operator<(std::nullptr_t) const { return mPtr < nullptr; }

    /// Access the object referenced by this reference.
    T* operator->() const { return mPtr; }

    /// Return a C++ reference to the referenced object.
    T& operator*() const { return *mPtr; }

    /// Return a pointer to the referenced object.
    T* get() const { return mPtr; }

    /// Check if the object is defined
    operator bool() const { return mPtr != nullptr; }

    /// Swap this reference with another reference.
    void swap(ref& r) noexcept
    {
        std::swap(mPtr, r.mPtr);
    }

private:
    inline void incRef(const Object* object)
    {
        object->incRef();
    }

    inline void decRef(const Object* object)
    {
        object->decRef();
    }

    T* mPtr = nullptr;

    template<typename T2>
    friend class ref;
};

template<class T, class... Args>
ref<T> make_ref(Args&&... args)
{
    return ref<T>(new T(std::forward<Args>(args)...));
}

template<class T, class U>
ref<T> static_ref_cast(const ref<U>& r) noexcept
{
    return ref<T>(static_cast<T*>(r.get()));
}

template<class T, class U>
ref<T> dynamic_ref_cast(const ref<U>& r) noexcept
{
    return ref<T>(dynamic_cast<T*>(r.get()));
}

template <typename T>
class BreakableReference
{
public:
    BreakableReference(const ref<T>& object) : mStrongRef(object), mWeakRef(mStrongRef.get()) {}
    BreakableReference(ref<T>&& object) : mStrongRef(std::move(object)), mWeakRef(mStrongRef.get()) {}

    BreakableReference() = delete;
    BreakableReference& operator=(const ref<T>&) = delete;
    BreakableReference& operator=(ref<T>&&) = delete;

    T* get() const { return mWeakRef; }
    T* operator->() const { return get(); }
    T& operator*() const { return *get(); }
    operator ref<T>() const { return ref<T>(get()); }
    operator T*() const { return get(); }
    operator bool() const { return get() != nullptr; }

    void breakStrongReference() { mStrongRef.reset(); }

private:
    ref<T> mStrongRef;
    T* mWeakRef = nullptr;
};
}  // namespace Falcor
