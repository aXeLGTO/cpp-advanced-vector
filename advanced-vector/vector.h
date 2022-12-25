#pragma once
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <new>
#include <utility>
#include <memory>
#include <algorithm>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity) :
        buffer_(Allocate(capacity)),
        capacity_(capacity) {
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;

    RawMemory(RawMemory&& other) noexcept :
        buffer_(std::exchange(other.buffer_, nullptr)),
        capacity_(std::exchange(other.capacity_, 0)) {
    }

    RawMemory& operator=(RawMemory&& rhs) noexcept {
        if (&rhs != this) {
            buffer_ = std::exchange(rhs.buffer_, buffer_);
            capacity_ = std::exchange(rhs.capacity_, capacity_);
        }
        return *this;
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    Vector() = default;

    explicit Vector(size_t size) :
        data_(size),
        size_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    ~Vector() noexcept {
        std::destroy_n(data_.GetAddress(), size_);
    }

    Vector(const Vector& other) :
        data_(other.size_),
        size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                auto rhs_copy(rhs);
                Swap(rhs_copy);
            } else {
                if (size_ > rhs.size_) {
                    std::copy_n(rhs.data_.GetAddress(), rhs.size_, data_.GetAddress());
                    std::destroy_n(data_ + rhs.size_, size_ - rhs.size_);
                } else {
                    std::copy_n(rhs.data_.GetAddress(), size_, data_.GetAddress());
                    std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_, data_.GetAddress());
                }
                size_ = rhs.size_;
            }
        }
        return *this;
    }

    Vector(Vector&& other) noexcept :
        data_(std::move(other.data_)),
        size_(std::exchange(other.size_, 0)) {
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }

    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_ + size_;
    }

    const_iterator begin() const noexcept {
        return cbegin();
    }

    const_iterator end() const noexcept {
        return cend();
    }

    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator cend() const noexcept {
        return data_ + size_;
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    template<typename Ref>
    void PushBack(Ref&& value) {
        EmplaceBack(std::forward<Ref>(value));
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        if (size_ == Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            new (new_data + size_) T(std::forward<Args>(args)...);

            CopyN(data_.GetAddress(), size_, new_data.GetAddress());
            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
        } else {
            new (data_ + size_) T(std::forward<Args>(args)...);
        }

        ++size_;

        return (*this)[size_ - 1];
    }

    void PopBack() noexcept {
        data_[--size_].~T();
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        size_t distance = pos - data_.GetAddress();

        if (size_ == Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            auto* tmp = new (new_data + distance) T(std::forward<Args>(args)...);

            try {
                CopyN(data_.GetAddress(), distance, new_data.GetAddress());
            } catch(...) {
                tmp->~T();
                throw;
            }

            try {
                CopyN(data_ + distance, size_ - distance, new_data.GetAddress() + distance + 1);
            } catch(...) {
                std::destroy_n(new_data.GetAddress(), distance + 1);
                throw;
            }

            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
        } else {
            if (distance < size_) {
                new (data_ + size_) T(std::move(*(end() - 1)));
                std::move_backward(const_cast<iterator>(pos), end() - 1, end());
                data_[distance] = std::move(T(std::forward<Args>(args)...));
            } else {
                new (data_ + distance) T(std::forward<Args>(args)...);
            }
        }

        ++size_;

        return data_ + distance;
    }

    template <typename Ref>
    iterator Insert(const_iterator pos, Ref&& value) {
        return Emplace(pos, std::forward<Ref>(value));
    }

    iterator Erase(const_iterator pos) /*noexcept(std::is_nothrow_move_assignable_v<T>)*/ {
        std::move(std::next(const_cast<iterator>(pos)), end(), const_cast<iterator>(pos));
        data_[--size_].~T();
        return const_cast<iterator>(pos);
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }

        RawMemory<T> new_data(new_capacity);
        CopyN(data_.GetAddress(), size_, new_data.GetAddress());
        std::destroy_n(data_.GetAddress(), size_);

        data_.Swap(new_data);
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy_n(data_ + new_size, size_ - new_size);
        } else {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_ + size_, new_size - size_);
        }
        size_ = new_size;
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

private:
    static void CopyN(T* data, size_t n, T* new_data) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data, n, new_data);
        } else {
            std::uninitialized_copy_n(data, n, new_data);
        }
    }

    RawMemory<T> data_;
    size_t size_ = 0;
};
