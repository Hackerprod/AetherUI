#pragma once

#include <aether/aether.h>

/* Internal parser entrypoints.  The public ABI exposes the resulting typed
   values and their ownership functions; CSS source parsing belongs to the
   style runtime. */
bool aether_transform_list_try_parse(const char* source, aether_transform_list* out);
bool aether_transform_origin_try_parse(const char* source, aether_transform_origin* out);
