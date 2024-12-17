#ifndef DEBUG_TRACE_H
#define DEBUG_TRACE_H

#include <stdint.h>

typedef enum {
  StopOnFull = 0,
  RollOver = 1,
} TraceBufferMode;

void DebugTraceInit(TraceBufferMode trace_buffer_mode, uint32_t trace_buffer_addr, uint32_t trace_buffer_size);
#endif
