#include "stdafx.h"
#include "ep_defines.h"
#include "ep_account.h"
#include "ep_player.h"
#include "ep_listener.h"

PlayerElement player_t::generate_player_element()
{
	PlayerElement res = {};
	res.flags = account->flags;
	res.name = account->uniq_name.length ? account->uniq_name : str_t<48>::make(account->name.data, account->name.length);
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
		m_list[index] = nullptr;
		return true;
	}

	return false;
}

void player_list_t::generate_player_list(ServerListRec& plist, u32 self)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	plist.code = SERVER_PLIST;
	plist.size = static_cast<u16>(8 + sizeof(PlayerElement) * m_list.size());
	plist.self = self;
	plist.count = static_cast<u32>(m_list.size());

	PlayerElement empty = { {}, 0, -1 };

	for (u32 i = 0; i < m_list.size(); i++)
	{
		plist.data[i] = m_list[i] ? m_list[i]->generate_player_element() : empty;
	}
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
