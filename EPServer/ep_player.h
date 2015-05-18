#pragma once
#include "ep_defines.h"

class account_t;

class player_t final
{
public:
	std::shared_ptr<account_t> account;
	u32 index;

	PlayerElement generate_player_element();
};

class player_list_t final
{
	std::mutex m_mutex;
	std::vector<std::shared_ptr<player_t>> m_list;

public:
	std::shared_ptr<player_t> add_player(const std::shared_ptr<account_t>& account);

	bool remove_player(u32 index);

	packet_data_t generate_player_list(u32 self);

	std::string get_name_by_index(u32 index);
};
