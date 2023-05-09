#pragma once

#include <stdbool.h>
#include <uv.h>

bool ship_server_init(uv_loop_t *loop, int port);
