#ifndef IP_DIAGNOSTICS_TEST_FREERTOS_H
#define IP_DIAGNOSTICS_TEST_FREERTOS_H
#include <assert.h>
typedef int portMUX_TYPE;
extern unsigned diagnostic_lock_depth;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) do { (void)(lock); assert(!diagnostic_lock_depth); ++diagnostic_lock_depth; } while (0)
#define portEXIT_CRITICAL(lock) do { (void)(lock); assert(diagnostic_lock_depth == 1); --diagnostic_lock_depth; } while (0)
#endif
