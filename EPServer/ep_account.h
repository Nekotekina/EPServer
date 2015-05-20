#pragma once
#include "ep_defines.h"

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

	std::string get_name()
	{
		return uniq_name.length ? std::string(uniq_name.data, uniq_name.length) : std::string(name.data, name.length);
	}
};

class account_list_t final
{
	std::mutex m_mutex;
	std::vector<std::shared_ptr<account_t>> m_list;

public:
	bool save();
	bool load();
	void lock();

	std::shared_ptr<account_t> add_account(short_str_t<16> name, md5_t pass);

	u32 size() const
	{
		return static_cast<u32>(m_list.size());
	}
};
