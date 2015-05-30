#pragma once
#include "ep_defines.h"

class account_t;
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

	void assign_player_element(PlayerElement& info);

	void append_connection_info(std::string& info);

	bool add_listener(std::shared_ptr<listener_t> listener);

	player_state_t remove_listener(const listener_t* listener);

	void broadcast(packet_t packet);

	void broadcast(const std::string& text)
	{
		broadcast(ServerTextRec::generate(GetTime(), text));
	}
};

class player_list_t final
{
	std::mutex m_mutex;
	std::vector<std::shared_ptr<player_t>> m_list;

public:
	std::shared_ptr<player_t> add_player(const std::shared_ptr<account_t>& account);

	bool remove_player(u32 index);

	packet_t generate_player_list(u32 self);

	void update_player(const std::shared_ptr<player_t>& player, bool removed = false);

	std::shared_ptr<player_t> get_player(u32 index);

	void broadcast(packet_t packet, std::function<bool(player_t&)> pred = nullptr);

	void broadcast(const std::string& text, std::function<bool(player_t&)> pred = nullptr)
	{
		broadcast(ServerTextRec::generate(GetTime(), text), pred);
	}
};
