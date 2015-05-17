#pragma once

#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS // TODO
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <string>
#include <array>
#include <memory>
#include <vector>
#include <queue>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <algorithm>

#ifndef NOMINMAX
#define NOMINMAX // TODO
#endif

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

using f32 = float;
using f64 = double;
