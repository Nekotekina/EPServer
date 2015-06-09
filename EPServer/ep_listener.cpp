#include "stdafx.h"
#include "ep_defines.h"
#include "ep_account.h"
#include "ep_player.h"
#include "ep_listener.h"

listener_t::listener_t(u32 addr, u16 port, bool enc)
	: addr(addr)
	, port(port)
	, enc(enc)
{
	quit_flag.clear();
	stop_flag.clear();
}

void listener_t::push_packet(packet_t packet)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	m_queue.emplace(std::move(packet));

	m_cond.notify_one();
}

void listener_t::push(const void* data, u32 size)
{
	packet_t packet(size);

	std::memcpy(packet->data(), data, size);

	push_packet(std::move(packet));
}

packet_t listener_t::pop(u32 timeout_ms, const packet_t& default_packet)
{
	std::unique_lock<std::mutex> lock(m_mutex);

	m_cond.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&]
	{
		return !m_queue.empty();
	});

	if (m_queue.empty())
	{
		return default_packet;
	}

	packet_t packet = std::move(m_queue.front());
	m_queue.pop();
	return packet;
}
