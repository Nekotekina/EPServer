#include "stdafx.h"
#include "ep_defines.h"
#include "ep_account.h"
#include "ep_player.h"
#include "ep_listener.h"

player_t::player_t(const std::shared_ptr<account_t>& account, u32 index)
	: account(account)
	, index(index)
	, conn_count(0)
{
	account->flags |= PF_LOST; // crutch
}

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
			return p;
		}
	}

	u32 index = 0;

	for (; index < m_list.size(); index++)
	{
		if (!m_list[index])
		{
			m_list[index].reset(new player_t(account, index));
			return m_list[index];
		}
	}

	if (m_list.size() == MAX_PLAYERS)
	{
		return nullptr;
	}

	m_list.emplace_back(new player_t(account, index));
	return m_list.back();
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

	const u16 hsize = static_cast<u16>(8 + sizeof(PlayerElement) * m_list.size());

	packet_data_t packet(hsize + 3);

	auto data = packet.get<ServerListRec>();
	data->header = { SERVER_PLIST, hsize };
	data->self = self;
	data->count = static_cast<u32>(m_list.size());

	PlayerElement empty = { {}, 0, -1 };

	for (u32 i = 0; i < m_list.size(); i++)
	{
		data->data[i] = m_list[i] ? m_list[i]->generate_player_element() : empty;
	}

	return packet;
}

std::shared_ptr<player_t> player_list_t::get_player(u32 index)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	return index < m_list.size() ? m_list[index] : nullptr;
}
