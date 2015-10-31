#pragma once

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS // TODO
#endif

#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cstdint>
#include <climits>
#include <cstddef>
#include <cstring>
#include <ctime>
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

// C++ Format Library https://github.com/cppformat/cppformat
#define FMT_USE_IOSTREAMS 0
#include "format.h"

#ifndef NOMINMAX
#define NOMINMAX // TODO
#endif

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

using f32 = float;
using f64 = double;

struct FILE_deleter
{
	void operator()(std::FILE* f) const
	{
		std::fclose(f);
	}
};

using unique_FILE = std::unique_ptr<std::FILE, FILE_deleter>;

void print_time();

// Print logs with current time
template<typename... T> inline void ep_printf(const char* fmt, const T&... args)
{
	print_time();
	fmt::print(fmt, args...);
}

inline u32 rol32(u32 v, u32 s)
{
	return (v << s) | (v >> (32 - s));
}

inline u32 ror32(u32 v, u32 s)
{
	return (v >> s) | (v << (32 - s));
}
