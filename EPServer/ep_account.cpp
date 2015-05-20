#include "stdafx.h"
#include "ep_defines.h"
#include "ep_account.h"

void account_t::save(std::FILE* f)
{
	u32 size = name.length + 1 + sizeof(pass) + sizeof(flags) + uniq_name.length + 1 + email.length + 1;
	std::fwrite(&size, 1, sizeof(size), f);

	auto _flags = flags.load(std::memory_order_relaxed);
	std::fwrite(&_flags, 1, sizeof(_flags), f);
	std::fwrite(pass.data(), 1, pass.size(), f);

	name.save(f);
	uniq_name.save(f);
	email.save(f);
}

bool account_t::load(std::FILE* f)
{
	u32 size;

	if (std::fread(&size, 1, sizeof(u32), f) != sizeof(u32))
	{
		return false;
	}

	size_t read = 0;

	u64 _flags;
	read += std::fread(&_flags, 1, sizeof(_flags), f);
	flags.store(_flags, std::memory_order_relaxed);
	read += std::fread(pass.data(), 1, pass.size(), f);

	read += name.load(f);
	read += uniq_name.load(f);
	read += email.load(f);

	if (read > size)
	{
		return false;
	}

	//std::fseek(f, size - read, SEEK_CUR);

	return true;
}

bool account_list_t::save()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	unique_FILE f(std::fopen("account.dat", "wb"));

	if (!f)
	{
		printf("account.dat writing failed!");
		return false;
	}

	for (auto& acc : m_list)
	{
		acc->save(f.get());
	}

	return true;
}

bool account_list_t::load()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	unique_FILE f(std::fopen("account.dat", "rb"));

	if (!f)
	{
		printf("account.dat not found!\n");
		return false;
	}

	while (!std::feof(f.get()))
	{
		std::shared_ptr<account_t> acc(new account_t);

		if (!acc->load(f.get()))
		{
			break;
		}

		m_list.emplace_back(std::move(acc));
	}

	return true;
}

void account_list_t::lock()
{
	return m_mutex.lock();
}

std::shared_ptr<account_t> account_list_t::add_account(short_str_t<16> name, md5_t pass)
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
