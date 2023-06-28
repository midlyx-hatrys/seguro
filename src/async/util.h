#pragma once

#define container_of(ptr, type, member) \
  ((type *)((uint8_t *)(ptr) - offsetof(type, member)))

#define _minmax(a, b, op)                               \
  ({ __auto_type _a = (a); __auto_type _b = (b);        \
    _a op _b ? _a : b; })
#define min(a, b) _minmax(a, b, <)
#define max(a, b) _minmax(a, b, >)

#define NAME_AND_COMMA(TOKEN) TOKEN ,
#define STRING_AND_COMMA(TOKEN) #TOKEN ,
