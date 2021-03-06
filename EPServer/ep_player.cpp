#include "stdafx.h"
#include "ep_defines.h"
#include "ep_socket.h"
#include "ep_account.h"
#include "ep_player.h"
#include "ep_listener.h"

player_t::player_t(const std::shared_ptr<account_t>& account, u32 index)
	: account(account)
	, index(index)
{
	account->flags |= PF_LOST; // crutch
}

void player_t::assign_player_element(PlayerElement& info, const std::unique_lock<account_list_t>& acc_lock)
{
	if (account->uniq_name.size())
	{
		info.name = account->uniq_name;
	}
	else
	{
		info.name = account->name;
	}

	info.flags = account->flags & ~PF_HIDDEN_FLAGS;
	info.gindex = -1;
}

void player_t::append_connection_info(std::string& info)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	for (const auto& listener : m_list)
	{
		inaddr_t addr;
		addr.s_addr = listener->addr;

		info += fmt::format("\nConnection: {}:{}{}", inet_ntoa(addr), listener->port, listener->enc ? " (encrypted)" : "");
	}
}

bool player_t::add_listener(std::shared_ptr<listener_t> listener)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_list.size() >= 4) // rough limitation
	{
		return false;
	}

	return m_list.emplace_back(std::move(listener)), true;
}

player_state_t player_t::remove_listener(const std::shared_ptr<listener_t>& listener)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	for (auto i = m_list.begin(); i != m_list.end(); i++)
	{
		if (*i == listener)
		{
			m_list.erase(i);
			break;
		}
	}

	if (m_list.size())
	{
		return PS_CONNECTED;
	}

	if (~account->flags.fetch_or(PF_LOST) & PF_LOST)
	{
		return PS_CONNECTION_LOST;
	}
	else
	{
		return PS_DISCONNECTED;
	}
}

void player_t::broadcast(packet_t packet)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	for (auto& listener : m_list)
	{
		listener->push_packet(packet);
	}
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
			return m_list[index] = std::make_shared<player_t>(account, index);
		}
	}

	if (m_list.size() == MAX_PLAYERS)
	{
		return nullptr;
	}

	return m_list.emplace_back(std::make_shared<player_t>(account, index)), m_list.back();
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

packet_t player_list_t::generate_player_list(u32 self, const std::unique_lock<account_list_t>& acc_lock)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	const u16 hsize = static_cast<u16>(8 + sizeof(PlayerElement) * m_list.size());

	packet_t packet(hsize + 3);

	auto& data = packet->get<ServerListRec>();
	data.header = { SERVER_PLIST, hsize };
	data.self = self;
	data.count = static_cast<u32>(m_list.size());

	auto info = data.data;

	for (auto& player : m_list)
	{
		if (player)
		{
			player->assign_player_element(*info++, acc_lock);
		}
		else
		{
			*info++ = { {}, 0, -1 };
		}
	}

	return packet;
}

void player_list_t::update_player(const std::shared_ptr<player_t>& player, const std::unique_lock<account_list_t>& acc_lock, bool removed)
{
	packet_t packet(sizeof(ServerUpdatePlayer));

	auto& data = packet->get<ServerUpdatePlayer>();
	data.header.code = SERVER_PUPDATE;
	data.header.size = sizeof(ServerUpdatePlayer) - 3;
	data.index = player->index;

	if (removed)
	{
		data.data = { {}, 0, -1 };
	}
	else
	{
		player->assign_player_element(data.data, acc_lock);
	}

	broadcast(std::move(packet));
}

std::shared_ptr<player_t> player_list_t::get_player(u32 index)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	return index < m_list.size() ? m_list[index] : nullptr;
}
