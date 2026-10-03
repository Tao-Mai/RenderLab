#include "core/Logger.h"

#include <gtest/gtest.h>

namespace
{
struct DebugState
{
    DEBUG_ONLY(bool inited = false;)

    void init() { DEBUG_EXEC(inited = true); }
    void shutdown() { DEBUG_EXEC(inited = false); }
    ~DebugState() { DCHECK(!inited); }
};

template <class T>
concept HasDebugState = requires(T value) { value.inited; };

#ifndef NDEBUG
static_assert(HasDebugState<DebugState>);
#else
static_assert(!HasDebugState<DebugState>);
#endif
}

TEST(DebugMacros, DeclarationsAndStatementsFollowTheBuildMode)
{
    DebugState state;
    state.init();
    DCHECK(state.inited);
    state.shutdown();

    DEBUG_ONLY(int first = 0, second = 0;)
    DEBUG_EXEC(++first; ++second);
    DCHECK(first == 1 && second == 1);

    int executed = 0;
    if (true)
        DEBUG_EXEC(++executed);
    else
        executed = 100;

#ifndef NDEBUG
    EXPECT_EQ(executed, 1);
#else
    EXPECT_EQ(executed, 0);
    DCHECK(undeclaredCondition, "{}", undeclaredMessage());
#endif
}
