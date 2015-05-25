#include "stdafx.h"
#include "ep_defines.h"
#include "ep_account.h"

void account_t::save(std::FILE* f)
{
	u32 size = name.length + 1 + 16 + 8 + uniq_name.length + 1 + email.length + 1;
	std::fwrite(&size, 1, 4, f);

	u64 _flags = flags.load(std::memory_order_relaxed) & ~PF_VOLATILE_FLAGS;
	std::fwrite(&_flags, 1, 8, f);
	std::fwrite(pass.data(), 1, pass.size(), f);

	name.save(f);
	uniq_name.save(f);
	email.save(f);
}

bool account_t::load(std::FILE* f)
{
	u32 size;
	if (std::fread(&size, 1, 4, f) != 4)
	{
		return false;
	}

	std::size_t read = 0;

	u64 _flags;
	read += std::fread(&_flags, 1, 8, f);
	flags.store(_flags & ~PF_VOLATILE_FLAGS, std::memory_order_relaxed);
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
		std::printf("account.dat writing failed!");
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
		std::printf("account.dat not found!\n");
		return false;
	}

	while (!std::feof(f.get()))
	{
		auto acc = std::make_shared<account_t>();

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

	ep_printf("New account registered: %s\n", name.c_str().get());

	auto acc = std::make_shared<account_t>();

	acc->name = name;
	acc->pass = pass;
	acc->flags = m_list.empty() ? PF_SUPERADMIN : PF_NEW_PLAYER;
	acc->uniq_name = {};
	acc->email = {};

	m_list.emplace_back(acc);
	return acc;
}
