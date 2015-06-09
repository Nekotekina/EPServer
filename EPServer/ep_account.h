#pragma once
#include "ep_defines.h"

class account_list_t;

class account_t final
{
public:
	short_str_t<16> name;

	md5_t pass;
	std::atomic<u64> flags;

	short_str_t<48> uniq_name;
	short_str_t<255> email;

	void save(std::FILE* f);
	bool load(std::FILE* f);

	std::string get_name(const std::unique_lock<account_list_t>& acc_lock)
	{
		return uniq_name.length ? std::string(uniq_name) : std::string(name);
	}
};

class account_list_t final
{
	std::mutex m_mutex;
	std::vector<std::shared_ptr<account_t>> m_list;

public:
	bool save(const std::unique_lock<account_list_t>& acc_lock);
	bool load();
	void lock() { return m_mutex.lock(); }
	void unlock() { return m_mutex.unlock(); }

	std::shared_ptr<account_t> add_account(const short_str_t<16>& name, const md5_t& pass);

	std::size_t size() const
	{
		return m_list.size();
	}
};
