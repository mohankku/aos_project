
#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER sample_tracepoint

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "./sample_tracepoint.h"

#if !defined(SAMPLE_TRACEPOINT_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define SAMPLE_TRACEPOINT_H

#include <lttng/tracepoint.h>

TRACEPOINT_EVENT(
        sample_tracepoint,
        message, // C++ Style comment
        TP_ARGS(char *, text),
        TP_FIELDS(
                ctf_string(message, text)
                  )
)
/*
 * Longer comments
 */
TRACEPOINT_LOGLEVEL(
        sample_tracepoint,
        message,
        TRACE_WARNING)

#endif /* SAMPLE_TRACEPOINT_H */

#include <lttng/tracepoint-event.h>
