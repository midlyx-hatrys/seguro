#pragma once

#include <stdbool.h>

#define container_of(ptr, type, member) ({                      \
      const typeof( ((type *)0)->member ) *__mptr = (ptr);      \
      (type *)( (char *)__mptr - offsetof(type,member) );})

#define likely(expr) \
  __builtin_expect(expr, true)
#define unlikely(expr) \
  __builtin_expect(expr, false)

#define _minmax(a, b, op)                               \
  ({ __auto_type _a = (a); __auto_type _b = (b);        \
    _a op _b ? _a : b; })
#define min(a, b) _minmax(a, b, <)
#define max(a, b) _minmax(a, b, >)

#define NAME_AND_COMMA(TOKEN) TOKEN ,
#define STRING_AND_COMMA(TOKEN) #TOKEN ,
