#include "stdafx.h"

void print_time()
{
	const std::time_t now = std::time(0); // get current time

	char time_buf[20];
	std::strftime(time_buf, sizeof(time_buf), "%d-%m-%Y %H:%M:%S", std::localtime(&now));

	std::printf("[%s] ", time_buf);
}
