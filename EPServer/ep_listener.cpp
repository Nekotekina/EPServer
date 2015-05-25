#include "stdafx.h"
#include "ep_defines.h"
#include "ep_account.h"
#include "ep_player.h"
#include "ep_listener.h"

listener_t::listener_t(const std::shared_ptr<player_t>& player)
	: player(player)
{
	quit_flag.clear();
}

void listener_t::push_packet(packet_t packet)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	m_queue.emplace(std::move(packet));

	m_cond.notify_one();
}

void listener_t::push(const void* data, u32 size)
{
	packet_t packet = make_packet(size);

	std::memcpy(packet->get(), data, size);

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


std::shared_ptr<listener_t> listener_list_t::add_listener(const std::shared_ptr<player_t>& player)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (player->conn_count++ >= 4) // rough limitation
	{
		player->conn_count--;
		return nullptr;
	}

	m_list.emplace_back(std::make_shared<listener_t>(player));
	return m_list.back();
}

u32 listener_list_t::remove_listener(const listener_t* listener)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	for (auto i = m_list.begin(); i != m_list.end(); i++)
	{
		if (i->get() == listener)
		{
			// remove listener and return new conn_count value
			return m_list.erase(i), --listener->player->conn_count;
		}
	}

	// TODO: this shouldn't happen
	return listener->player->conn_count;
}

void listener_list_t::update_player(const std::shared_ptr<player_t>& player, bool removed)
{
	packet_t packet = make_packet(sizeof(ServerUpdatePlayer));

	const auto data = packet->get<ServerUpdatePlayer>();
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
	const packet_t packet = ServerTextRec::generate(GetTime(), text);

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
		listener->push(ProtocolHeader{ SERVER_NONFATALDISCONNECT });
		listener->stop();
	}
}
