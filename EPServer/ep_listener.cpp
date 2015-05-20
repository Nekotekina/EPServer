#include "stdafx.h"
#include "ep_defines.h"
#include "ep_account.h"
#include "ep_player.h"
#include "ep_listener.h"

listener_t::listener_t(const std::shared_ptr<player_t>& player)
	: player(player)
{
}

void listener_t::push_packet(const packet_t& packet)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	m_queue.push(packet);

	m_cond.notify_one();
}

void listener_t::push(const void* data, u32 size)
{
	packet_t packet(new packet_data_t(size));

	memcpy(packet->get(), data, size);

	push_packet(packet);
}

void listener_t::push_text(const std::string& text)
{
	const u16 size = static_cast<u16>(std::min<size_t>(text.size(), ServerTextRec::max_data_size)); // text size

	packet_t packet(new packet_data_t(size + 11));

	auto data = packet->get<ServerTextRec>();
	data->header.code = SERVER_TEXT;
	data->header.size = size + sizeof(f64);
	data->stamp = GetTime();
	memcpy(data->data, text.c_str(), size);

	push_packet(packet);
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


std::shared_ptr<listener_t> listener_list_t::add_listener(const std::shared_ptr<player_t>& player)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (player->conn_count++ >= 4) // rough limitation
	{
		player->conn_count--;
		return nullptr;
	}

	m_list.emplace_back(new listener_t(player));

	return m_list.back();
}

void listener_list_t::remove_listener(const listener_t* listener)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	for (auto i = m_list.begin(); i != m_list.end(); i++)
	{
		if (i->get() == listener)
		{
			listener->player->conn_count--;

			m_list.erase(i);
			return;
		}
	}
}

void listener_list_t::update_player(const std::shared_ptr<player_t>& player, bool removed)
{
	packet_t packet(new packet_data_t(sizeof(ServerUpdatePlayer)));

	auto data = packet->get<ServerUpdatePlayer>();
	data->header.code = SERVER_PUPDATE;
	data->header.size = sizeof(ServerUpdatePlayer) - 3;
	data->index = player->index;
	data->data = removed ? PlayerElement{} : player->generate_player_element();

	std::lock_guard<std::mutex> lock(m_mutex);

	for (auto& listener : m_list)
	{
		listener->push_packet(packet);
	}
}

void listener_list_t::broadcast(const std::string& text, const std::function<bool(listener_t&)> pred)
{
	const u16 size = static_cast<u16>(std::min<size_t>(text.size(), ServerTextRec::max_data_size)); // text size

	packet_t packet(new packet_data_t(size + 11));

	auto data = packet->get<ServerTextRec>();
	data->header.code = SERVER_TEXT;
	data->header.size = size + sizeof(f64);
	data->stamp = GetTime();
	memcpy(data->data, text.c_str(), size);

	std::lock_guard<std::mutex> lock(m_mutex);

	for (auto& listener : m_list)
	{
		if (pred(*listener))
		{
			listener->push_packet(packet);
		}
	}
}

void listener_list_t::stop_all()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	for (auto& listener : m_list)
	{
		listener->stop();
	}
}
