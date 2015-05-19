#include "stdafx.h"
#include "ep_defines.h"
#include "ep_account.h"
#include "ep_player.h"
#include "ep_listener.h"

PlayerElement player_t::generate_player_element()
{
	PlayerElement res = {};
	res.flags = account->flags;
	account->uniq_name.length
		? res.name = account->uniq_name
		: res.name = account->name;
	res.gindex = -1;
	return res;
}


std::shared_ptr<player_t> player_list_t::add_player(const std::shared_ptr<account_t>& account)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	for (auto& p : m_list)
	{
		if (p && p->account == account)
		{
			//p->conn_count++;
			return p;
		}
	}

	std::shared_ptr<player_t> player(new player_t);
	player->account = account;
	//player->conn_count = 1;

	for (u32& i = player->index = 0; i < m_list.size(); i++)
	{
		if (!m_list[i])
		{
			m_list[i] = player;
			return player;
		}
	}

	if (m_list.size() == MAX_PLAYERS)
	{
		return nullptr;
	}

	m_list.push_back(player);
	return player;
}

bool player_list_t::remove_player(u32 index)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (index < m_list.size() && m_list[index])
	{
		m_list[index].reset();
		return true;
	}

	return false;
}

packet_data_t player_list_t::generate_player_list(u32 self)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	packet_data_t packet(static_cast<u32>(11 + sizeof(PlayerElement) * m_list.size()));

	auto data = packet.get<ServerListRec>();
	data->header.code = SERVER_PLIST;
	data->header.size = static_cast<u16>(8 + sizeof(PlayerElement) * m_list.size());
	data->self = self;
	data->count = static_cast<u32>(m_list.size());

	PlayerElement empty = { {}, 0, -1 };

	for (u32 i = 0; i < m_list.size(); i++)
	{
		data->data[i] = m_list[i] ? m_list[i]->generate_player_element() : empty;
	}

	return packet;
}

std::string player_list_t::get_name_by_index(u32 index)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (index < m_list.size() && m_list[index])
	{
		return m_list[index]->account->get_name();
	}
	else
	{
		return "Wrong index " + std::to_string(index);
	}
}
