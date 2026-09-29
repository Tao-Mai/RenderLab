//
// Created by 24500 on 2026/9/29.
//
#include <memory>

int main()
{
    std::shared_ptr<int> a;
    std::weak_ptr<int>   b(a);
    auto                 res = b.lock();
    auto                 e   = b.expired();
    b.reset();
}
