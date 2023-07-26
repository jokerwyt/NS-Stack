#pragma once

#define NO_WARN_CAST(type, dst, src)                                \
    _Pragma("GCC diagnostic push")                                  \
    _Pragma("GCC diagnostic ignored \"-Wpointer-to-int-cast\"")     \
    _Pragma("GCC diagnostic ignored \"-Wint-to-pointer-cast\"")     \
        (dst) = ((type) (src));                                     \
    _Pragma("GCC diagnostic pop")

