#include "stdafx.h"
#include "ep_socket.h"

void print_time_ip(inaddr_t ip, u16 port)
{
	print_time();
	std::printf("%16s:%-6d", inet_ntoa(ip), port);
}
