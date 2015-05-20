#pragma once
#include "ep_defines.h"

class player_t;

class listener_t final
{
	std::mutex m_mutex;
	std::queue<packet_t> m_queue;
	std::condition_variable m_cond;

public:
	const std::shared_ptr<player_t> player;

	explicit listener_t(const std::shared_ptr<player_t>& player);

	void push_packet(const packet_t& packet);

	void push(const void* data, u32 size);

	template<typename T> void push(const T& data)
	{
		push(&data, sizeof(T));
	}

	void push_text(const std::string& text);

	void stop()
	{
		push_packet(nullptr); // use empty message as stop message
	}

	packet_t pop(u32 timeout_ms, const packet_t& default_packet);
};

class listener_list_t final
{
	std::mutex m_mutex;
	std::vector<std::shared_ptr<listener_t>> m_list;

public:
	std::shared_ptr<listener_t> add_listener(const std::shared_ptr<player_t>& player);

	void remove_listener(const listener_t* listener);

	void update_player(const std::shared_ptr<player_t>& player, bool removed = false);

	void broadcast(const std::string& text, const std::function<bool(listener_t&)> pred);

	void stop_all();
};
