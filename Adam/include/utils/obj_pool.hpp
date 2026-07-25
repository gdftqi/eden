#ifndef __ADAM_UTILS_OBJ_POOL_HPP__
#define __ADAM_UTILS_OBJ_POOL_HPP__


#include <stack>
#include <mimalloc-3.2/mimalloc.h>
#include "utils/log.hpp"


namespace adam::utils {


template <typename T>
class ObjPool {
public:
    ~ObjPool() noexcept {
        while (!pool_.empty()) {
           ::mi_free(pool_.top());
            pool_.pop();
        }
    }


    template <typename... Args>
    T*
    acquire(Args&&... args) noexcept{
        T* obj = nullptr;
        if (pool_.empty()) {
            obj = (T*)::mi_malloc(sizeof(T));
            ASSERT(obj != nullptr, "mi_malloc failed");
            new(obj) T(std::forward<Args>(args)...);
        } else {
            obj = pool_.top();
            pool_.pop();
            new (obj) T(std::forward<Args>(args)...);
            
        }
        return obj;
    }


    void
    release(T* obj) noexcept {
        if (obj) {
            obj->~T();
            pool_.push(obj);
        }
    }


private:
     std::stack<T*> pool_;
}; // class ObjPool;


} // namespace adam::utils


#endif // __ADAM_UTILS_OBJ_POOL_HPP__
