#include "stdafx.h"
#include "ep_defines.h"
#include "ep_account.h"
#include "ep_player.h"
#include "ep_listener.h"

void listener_t::push_packet(const packet_t& packet)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		m_queue.push(packet);
	}

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

	auto data = reinterpret_cast<ServerTextRec*>(packet->get());
	data->code = SERVER_TEXT;
	data->size = size + sizeof(f64);
	data->stamp = GetTime();
	memcpy(data->data, text.c_str(), size);

	push_packet(packet);
}

packet_t listener_t::pop()
{
	std::unique_lock<std::mutex> lock(m_mutex);

	while (m_queue.empty())
	{
		m_cond.wait(lock);
	}

	packet_t packet = std::move(m_queue.front());
	m_queue.pop();
	return packet;
}


std::shared_ptr<listener_t> listener_list_t::add_listener(const std::shared_ptr<player_t>& player)
{
	std::shared_ptr<listener_t> listener(new listener_t);
	listener->player = player;

	if (player.use_count() > 8) // rough limitation
	{
		return nullptr;
	}

	std::lock_guard<std::mutex> lock(m_mutex);

	return m_list.push_back(listener), listener;
}

void listener_list_t::remove_listener(const listener_t* listener)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	for (auto i = m_list.begin(); i != m_list.end(); i++)
	{
		if (i->get() == listener)
		{
			//if (!--l->player->conn_count)
			//{
			//	l->player->account->flags |= PF_LOST;
			//}

			m_list.erase(i);
			return;
		}
	}
}

void listener_list_t::update_player(const std::shared_ptr<player_t>& player)
{
	packet_t packet(new packet_data_t(sizeof(ServerUpdatePlayer)));

	auto data = reinterpret_cast<ServerUpdatePlayer*>(packet->get());
	data->code = SERVER_PUPDATE;
	data->size = sizeof(ServerUpdatePlayer) - 3;
	data->index = player->index;
	data->data = player->generate_player_element();

	std::lock_guard<std::mutex> lock(m_mutex);

	for (auto& listener : m_list)
	{
		listener->push_packet(packet);
	}
}

void listener_list_t::broadcast(const std::string& text, const std::function<bool(listener_t*)> pred)
{
	const u16 size = static_cast<u16>(std::min<size_t>(text.size(), ServerTextRec::max_data_size)); // text size

	packet_t packet(new packet_data_t(size + 11));

	auto data = reinterpret_cast<ServerTextRec*>(packet->get());
	data->code = SERVER_TEXT;
	data->size = size + sizeof(f64);
	data->stamp = GetTime();
	memcpy(data->data, text.c_str(), size);

	std::lock_guard<std::mutex> lock(m_mutex);

	for (auto& listener : m_list)
	{
		if (pred(listener.get()))
		{
			listener->push_packet(packet);
		}
	}
}
