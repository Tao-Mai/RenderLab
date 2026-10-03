#pragma once

#ifndef NDEBUG
#define DEBUG_ONLY(...) __VA_ARGS__
#define DEBUG_EXEC(...) do { __VA_ARGS__; } while (0)
#else
#define DEBUG_ONLY(...)
#define DEBUG_EXEC(...) do {} while (0)
#endif
