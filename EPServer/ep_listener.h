#pragma once
#include "ep_defines.h"

class player_t;

class listener_t final
{
	std::mutex m_mutex;
	std::queue<packet_t> m_queue;
	std::condition_variable m_cond;

public:
	const u32 addr;
	const u16 port;
	const bool enc; // true if cipher_socket_t used

	std::atomic_flag quit_flag;
	std::atomic_flag stop_flag;

	listener_t(u32 addr, u16 port, bool enc);

	void push_packet(packet_t packet);

	void push(const void* data, u32 size);

	template<typename T> void push(const T& data)
	{
		push(&data, sizeof(T));
	}

	void push_text(const std::string& text)
	{
		push_packet(ServerTextRec::make(GetTime(), text));
	}

	void stop()
	{
		stop_flag.test_and_set();
		push_packet(nullptr); // use empty message as stop message
	}

	packet_t pop(u32 timeout_ms, const packet_t& default_packet);
};
