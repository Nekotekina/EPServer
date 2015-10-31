#include "stdafx.h"

void print_time()
{
	const std::time_t now = std::time(0); // get current time
	const std::tm* time = std::localtime(&now);

	fmt::print("[{:02}-{:02}-{} {:02}:{:02}:{:02}] ", time->tm_mday, time->tm_mon + 1, time->tm_year + 1900, time->tm_hour, time->tm_min, time->tm_sec);
}
