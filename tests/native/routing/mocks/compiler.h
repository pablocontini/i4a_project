#pragma once

/* TinyCC 0.9.27 lacks C11 assertions; retain their compile-time checks. */
#ifdef __TINYC__
#define ROUTING_ASSERT_JOIN_INNER(a, b) a##b
#define ROUTING_ASSERT_JOIN(a, b) ROUTING_ASSERT_JOIN_INNER(a, b)
#define _Static_assert(condition, message) \
    typedef char ROUTING_ASSERT_JOIN(routing_static_assert_, __LINE__)[(condition) ? 1 : -1]
#endif
