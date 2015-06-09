#pragma once
#include "ep_defines.h"

class account_t;
class account_list_t;
class listener_t;

enum player_state_t
{
	PS_CONNECTION_LOST,
	PS_DISCONNECTED,
	PS_CONNECTED,
};

class player_t final
{
	std::mutex m_mutex;
	std::vector<std::shared_ptr<listener_t>> m_list;

public:
	const std::shared_ptr<account_t> account;
	const u32 index;

	player_t(const std::shared_ptr<account_t>& account, u32 index);

	void assign_player_element(PlayerElement& info, const std::unique_lock<account_list_t>& acc_lock);

	void append_connection_info(std::string& info);

	bool add_listener(std::shared_ptr<listener_t> listener);

	player_state_t remove_listener(const std::shared_ptr<listener_t>& listener);

	void broadcast(packet_t packet);

	void broadcast(const std::string& text)
	{
		broadcast(ServerTextRec::make(GetTime(), text));
	}
};

class player_list_t final
{
	std::mutex m_mutex;
	std::vector<std::shared_ptr<player_t>> m_list;

	static bool all_players(player_t&)
	{
		return true;
	}

public:
	std::shared_ptr<player_t> add_player(const std::shared_ptr<account_t>& account);

	bool remove_player(u32 index);

	packet_t generate_player_list(u32 self, const std::unique_lock<account_list_t>& acc_lock);

	void update_player(const std::shared_ptr<player_t>& player, const std::unique_lock<account_list_t>& acc_lock, bool removed = false);

	std::shared_ptr<player_t> get_player(u32 index);

	template<typename T> inline void broadcast(packet_t packet, const T pred)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		for (auto& player : m_list)
		{
			if (player && pred(*player))
			{
				player->broadcast(packet);
			}
		}
	}

	void broadcast(packet_t packet)
	{
		broadcast(packet, all_players);
	}

	template<typename T> inline void broadcast(const std::string& text, const T pred)
	{
		broadcast(ServerTextRec::make(GetTime(), text), pred);
	}

	void broadcast(const std::string& text)
	{
		broadcast(text, all_players);
	}
};
