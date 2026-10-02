#include <meta>

#if !defined(__cpp_impl_reflection) || __cpp_impl_reflection < 202506L
#error C++26 static reflection must be enabled for project targets
#endif

struct ReflectionRecord
{
    int value;
};

static_assert(std::meta::is_type(^^ReflectionRecord));
static_assert(std::meta::type_of(^^ReflectionRecord::value) == ^^int);
static_assert(std::meta::identifier_of(^^ReflectionRecord::value) == "value");

int main()
{
    typename [:^^ReflectionRecord:] record{42};
    return record.[:^^ReflectionRecord::value:] == 42 ? 0 : 1;
}
