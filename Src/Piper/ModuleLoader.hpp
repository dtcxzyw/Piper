#pragma once
#include <memory>
#include <string>

template<typename T>
class Plugin final {
private:
    std::shared_ptr<T> mPtr;

public:
    Plugin(const std::string& name);
    const T* operator->() const {
        return mPtr.get();
    }
    T* operator->() {
        return mPtr.get();
    }
};
