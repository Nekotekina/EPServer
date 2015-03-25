#pragma once

class account_t final
{
public:
	str_t<16> name;

	md5_t pass;
	std::atomic<u64> flags;

	str_t<48> uniq_name;
	str_t<255> email;

	void save(std::FILE* f);
	void load(std::FILE* f);

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

	std::shared_ptr<account_t> add_account(str_t<16> name, md5_t pass);
};
