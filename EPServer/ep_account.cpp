#include "stdafx.h"
#include "ep_defines.h"
#include "ep_account.h"

void account_t::save(std::FILE* f)
{
	u32 size = name.length + 1 + sizeof(pass) + sizeof(flags) + uniq_name.length + 1 + email.length + 1;
	std::fwrite(&size, sizeof(size), 1, f);

	auto _flags = flags.load(std::memory_order_relaxed);
	std::fwrite(&_flags, sizeof(_flags), 1, f);
	std::fwrite(pass.data(), 1, pass.size(), f);

	name.save(f);
	uniq_name.save(f);
	email.save(f);
}

void account_t::load(std::FILE* f)
{
	u32 size;
	std::fread(&size, sizeof(u32), 1, f);

	size_t read = 0;

	auto _flags = flags.load(std::memory_order_relaxed);
	read += std::fread(&_flags, sizeof(_flags), 1, f);
	flags.store(_flags, std::memory_order_relaxed);
	read += std::fread(pass.data(), 1, pass.size(), f);

	read += name.load(f);
	read += uniq_name.load(f);
	read += email.load(f);

	// ???
}


bool account_list_t::save()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	auto f = std::fopen("account.dat", "w");

	if (!f)
	{
		// TODO: check error
		return false;
	}

	size_t count = m_list.size();
	std::fwrite(&count, sizeof(u32), 1, f);

	for (auto& acc : m_list)
	{
		acc->save(f);
	}

	std::fclose(f);
	return true;
}

bool account_list_t::load()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	auto f = std::fopen("account.dat", "r");

	if (!f)
	{
		// TODO: check error
		return false;
	}

	s32 count = 0;
	std::fread(&count, sizeof(count), 1, f);
	m_list.resize(count);

	for (auto& acc : m_list)
	{
		acc.reset(new account_t);
		acc->load(f);
	}

	std::fclose(f);
	return true;
}

std::shared_ptr<account_t> account_list_t::add_account(str_t<16> name, md5_t pass)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	for (auto& acc : m_list)
	{
		if (acc->name == name)
		{
			if (acc->pass != pass)
			{
				return nullptr;
			}

			return acc;
		}
	}

	std::shared_ptr<account_t> acc(new account_t);
	acc->name = name;
	acc->pass = pass;
	acc->flags = m_list.empty() ? PF_SUPERADMIN : PF_NEW_PLAYER;
	acc->uniq_name = {};
	acc->email = {};

	m_list.push_back(acc);
	return acc;
}
