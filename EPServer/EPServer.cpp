﻿#include "stdafx.h"
#include "ep_defines.h"
#include "ep_socket.h"
#include "ep_account.h"
#include "ep_player.h"
#include "ep_listener.h"
#include "hl_md5.h"

#pragma warning(push)
#pragma warning(disable : 4146 4800)
#include <mpirxx.h>
#pragma warning(pop)

#include "../git-version.inl"

const ServerVersionRec version_info{ SERVER_VERSIONINFO, sizeof(ServerVersionRec) - 3, { EP_VERSION } };

account_list_t g_accounts;
player_list_t g_players;
socket_t g_server;

void stop(int x);

packet_t g_keepalive_packet;
packet_t g_auth_packet; // open key + sign
mpz_class g_key_n; // open key
mpz_class g_key_d; // priv key
u32 g_key_size = 0; // key size (bytes)

bool only_online(player_t& player)
{
	return (player.account->flags & PF_OFF) == 0;
}

void receiver_thread(std::shared_ptr<socket_t> socket, std::shared_ptr<account_t> account, std::shared_ptr<player_t> player, std::shared_ptr<listener_t> listener)
{
	std::unique_ptr<listener_t, void(*)(listener_t*)> listener_stopper(listener.get(), [](listener_t* listener) // scope exit
	{
		listener->stop();
	});

	std::this_thread::sleep_for(std::chrono::seconds(1));

	std::string cached_name;

	auto set_online = [&]
	{
		const auto flags = account->flags.fetch_and(~PF_OFF);

		if (flags & PF_OFF)
		{
			g_players.update_player(player, std::unique_lock<account_list_t>(g_accounts));

			const auto& text = cached_name + "%/ is online.";

			if (~account->flags & PF_SHADOWBAN)
			{
				g_players.broadcast(text, only_online);
			}
			else
			{
				player->broadcast(text);
			}
		}
	};

	auto set_offline = [&]
	{
		const auto flags = account->flags.fetch_or(PF_OFF);

		if (~flags & PF_OFF)
		{
			g_players.update_player(player, std::unique_lock<account_list_t>(g_accounts));

			const auto& text = cached_name + "%/ is offline.";

			if (~account->flags & PF_SHADOWBAN)
			{
				g_players.broadcast(text, only_online);
			}

			player->broadcast(text);
		}
	};

	ProtocolHeader header;
	ClientCmdRec cmd;

	while (socket->flush(), socket->get(header))
	{
		// TODO: reset last activity time

		// Update cached name
		if (!(account->uniq_name.size() != 0 && account->uniq_name == cached_name) && !(account->name == cached_name))
		{
			cached_name = account->get_name(std::unique_lock<account_list_t>(g_accounts));
		}

		if (header.code == CLIENT_CMD && header.size >= 14)
		{
			const u16 text_size = header.size - 14;

			if (!socket->get(&cmd, header.size))
			{
				return;
			}

			switch (cmd.cmd)
			{
			case CMD_NONE: break;

			case CMD_CHAT:
			{
				const std::string message(cmd.data, text_size);

				if (message.find("%p") != std::string::npos)
				{
					listener->push_text("You cannot send %%p marker.");
					listener->push_text(message);
				}
				else if (message.find("%/") != std::string::npos)
				{
					listener->push_text("You cannot send %%/ marker.");
					listener->push_text(message);
				}
				else if (cmd.v0 == -1 && !cmd.v1 && !cmd.v2)
				{
					// public message
					if (account->flags & PF_NOCHAT)
					{
						listener->push_text("You cannot write public messages.");
						listener->push_text(message);
					}
					else
					{
						set_online();

						const auto& text = message.substr(0, 4) == "/me " || message.substr(0, 4) == u8"/я "
							? cached_name + "%/ " + message.substr(4)
							: cached_name + "%/ %bwrites:%x " + message;

						if (~account->flags & PF_SHADOWBAN)
						{
							g_players.broadcast(text, only_online);
						}
						else
						{
							player->broadcast(text);
						}
					}

					// ~207 ms + 1 ms per character
					std::this_thread::sleep_for(std::chrono::milliseconds(200 + header.size));
				}
				else if (cmd.v0 >= 0 && !cmd.v1 && !cmd.v2)
				{
					// private message
					if (account->flags & PF_NOPRIVCHAT)
					{
						listener->push_text("You cannot write private messages.");
						listener->push_text(message);
					}
					else if (const auto target = g_players.get_player(cmd.v0))
					{
						if (~account->flags & PF_SHADOWBAN || player == target)
						{
							target->broadcast(cached_name + "%/%p%g writes (private):%x " + message);
						}
					}
					else
					{
						listener->push_text("Invalid player.");
					}

					// ~201 ms + 0.25 ms per character
					std::this_thread::sleep_for(std::chrono::milliseconds(200 + header.size / 4));
				}
				else
				{
					listener->push_text("Invalid arguments.");
				}
				break;
			}
			case CMD_DICE:
			{
				if (cmd.v0 == -1 && !cmd.v2)
				{
					// public dice
					if (account->flags & PF_NOCHAT)
					{
						listener->push_text("You cannot write public messages.");
					}
					else
					{
						set_online();

						const auto& text = cached_name + "%/ throws " + FormatDice(cmd.v1);

						if (~account->flags & PF_SHADOWBAN)
						{
							g_players.broadcast(text, only_online);
						}
						else
						{
							player->broadcast(text);
						}
					}

					// 200 ms
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
				}
				else if ((cmd.v0 == -2 || cmd.v0 == player->index) && !cmd.v2)
				{
					// self dice
					listener->push_text("You throw " + FormatDice(cmd.v1));
				}
				else if (cmd.v0 >= 0 && !cmd.v2)
				{
					// private dice
					if (account->flags & PF_NOPRIVCHAT)
					{
						listener->push_text("You cannot write private messages.");
					}
					else if (const auto target = g_players.get_player(cmd.v0))
					{
						const std::string& dice = FormatDice(cmd.v1);

						if (~account->flags & PF_SHADOWBAN)
						{
							target->broadcast(cached_name + "%/%p throws " + dice + " to you (private)");
						}

						listener->push_text("You throw " + dice + "%/ to " + target->account->get_name(std::unique_lock<account_list_t>(g_accounts)));
					}
					else
					{
						listener->push_text("Invalid player.");
					}

					// 200 ms
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
				}
				else
				{
					listener->push_text("Invalid arguments.");
				}
				break;
			}
			case CMD_SHOUT:
			{
				if (account->flags & PF_SUPERADMIN)
				{
					g_players.broadcast(cached_name + "%/ %bwrites:%x " + std::string(cmd.data, text_size));
				}
				else
				{
					listener->push_text("Check your privilege.");
				}
				break;
			}
			case CMD_SET_EMAIL:
			{
				if (cmd.v0 == -1 && !cmd.v1 && !cmd.v2)
				{
					{
						std::unique_lock<account_list_t> acc_lock(g_accounts);

						account->email = { cmd.data, text_size };

						g_accounts.save(acc_lock);
					}

					listener->push_text("E-mail set:");
					listener->push_text({ cmd.data, std::min<u16>(255, text_size) });

					std::this_thread::sleep_for(std::chrono::seconds(1));
				}
				else if (!cmd.v1 && !cmd.v2)
				{
					if (account->flags & PF_SUPERADMIN)
					{
						// find cmd.v0 player and set email
						if (const auto target = g_players.get_player(cmd.v0))
						{
							{
								std::unique_lock<account_list_t> acc_lock(g_accounts);

								target->account->email = { cmd.data, text_size };

								g_accounts.save(acc_lock);
							}

							listener->push_text("E-mail set:"); // TODO (message)
							listener->push_text({ cmd.data, std::min<u16>(255, text_size) });
						}
						else
						{
							listener->push_text("Invalid player.");
						}
					}
					else
					{
						listener->push_text("Check your privilege.");
					}
				}
				else
				{
					listener->push_text("Invalid arguments.");
				}
				break;
			}
			case CMD_SET_PASSWORD:
			{
				if (cmd.v0 == -1 && !cmd.v1 && !cmd.v2 && text_size > 16)
				{
					// check old password and set new one
					md5_t old;

					// calculate md5(md5(password))
					HL_MD5_CTX ctx;
					MD5().MD5Init(&ctx);
					MD5().MD5Update(&ctx, reinterpret_cast<unsigned char*>(cmd.data) + 16, text_size - 16);
					MD5().MD5Final(old.data(), &ctx);
					MD5().MD5Init(&ctx);
					MD5().MD5Update(&ctx, old.data(), 16);
					MD5().MD5Final(old.data(), &ctx);

					if (old == account->pass)
					{
						{
							std::unique_lock<account_list_t> acc_lock(g_accounts);

							account->pass = *reinterpret_cast<md5_t*>(cmd.data);

							g_accounts.save(acc_lock);
						}

						listener->push_text("Password updated.");
					}
					else
					{
						listener->push_text("Invalid password.");
					}

					std::memset(cmd.data, 0, text_size);

					std::this_thread::sleep_for(std::chrono::seconds(4));
				}
				else if (!cmd.v1 && !cmd.v2 && text_size == 16)
				{
					if (account->flags & PF_SUPERADMIN)
					{
						// find cmd.v0 player and reset password
						if (const auto target = g_players.get_player(cmd.v0))
						{
							{
								std::unique_lock<account_list_t> acc_lock(g_accounts);

								target->account->pass = *reinterpret_cast<md5_t*>(cmd.data);

								g_accounts.save(acc_lock);
							}

							listener->push_text("Password updated."); // TODO (message)
						}
						else
						{
							listener->push_text("Invalid player.");
						}						
					}
					else
					{
						listener->push_text("Check your privilege.");
					}
				}
				else
				{
					listener->push_text("Invalid arguments.");
				}
				break;
			}
			case CMD_SET_FLAG:
			{
				if (account->flags & PF_SUPERADMIN)
				{
					const u64 flag = 1ull << cmd.v1;

					if (cmd.v1 < 64u && flag != PF_SUPERADMIN && !cmd.v2)
					{
						// find cmd.v0 player and change flag
						if (const auto target = g_players.get_player(cmd.v0))
						{
							std::unique_lock<account_list_t> acc_lock(g_accounts);

							// TODO (message)

							const u64 _flags = target->account->flags ^= flag;

							if ((flag & PF_HIDDEN_FLAGS) == 0)
							{
								target->broadcast("Flag [" + std::string(FlagName[cmd.v1]) + (_flags & flag ? "] has been set." : "] has been removed."));
							}

							listener->push_text("Flags: " + FormatFlags(_flags));

							g_players.update_player(target, acc_lock);

							g_accounts.save(acc_lock);
						}
						else
						{
							listener->push_text("Invalid player.");
						}
					}
					else
					{
						listener->push_text("Invalid arguments.");
					}
				}
				else
				{
					listener->push_text("Check your privilege.");
				}
				break;
			}
			case CMD_DISCONNECT:
			{
				if (account->flags & PF_SUPERADMIN)
				{
					// find cmd.v0 player and disconnect it
					if (const auto target = g_players.get_player(cmd.v0))
					{
						listener->push_text("Not implemented.");
					}
					else
					{
						listener->push_text("Invalid player.");
					}
				}
				else
				{
					listener->push_text("Check your privilege.");
				}
				break;
			}
			case CMD_INFO:
			{
				if (account->flags & PF_SUPERADMIN)
				{
					// find cmd.v0 player and display information
					if (const auto target = g_players.get_player(cmd.v0))
					{
						std::lock_guard<account_list_t> acc_lock(g_accounts);

						std::string info;

						info += "\nLogin: ";
						info += target->account->name;
						info += "\nName: ";
						info += target->account->uniq_name;
						info += "\nEmail: ";
						info += target->account->email;
						info += "\nFlags: ";
						info += FormatFlags(target->account->flags);
						
						target->append_connection_info(info);

						listener->push_text(info);
					}
					else
					{
						listener->push_text("Invalid player.");
					}
				}
				else
				{
					listener->push_text("Check your privilege.");
				}
				break;
			}
			case CMD_CHANGE:
			{
				if (account->flags & PF_SUPERADMIN)
				{
					if (const auto target = g_players.get_player(cmd.v0))
					{
						listener->push_text("Not implemented.");
					}
					else
					{
						listener->push_text("Invalid player.");
					}
				}
				else
				{
					listener->push_text("Check your privilege.");
				}
				break;
			}
			case CMD_SET_NAME:
			{
				if (account->flags & PF_SUPERADMIN)
				{
					// find cmd.v0 player and set unique name
					if (const auto target = g_players.get_player(cmd.v0))
					{
						{
							std::unique_lock<account_list_t> acc_lock(g_accounts);

							target->account->uniq_name = { cmd.data, text_size };

							g_players.update_player(target, acc_lock);

							g_accounts.save(acc_lock);
						}

						listener->push_text("Unique name set:"); // TODO (message)
						listener->push_text({ cmd.data, std::min<u16>(48, text_size) });
					}
					else
					{
						listener->push_text("Invalid player.");
					}
				}
				else
				{
					listener->push_text("Check your privilege.");
				}
				break;
			}
			case CMD_CALL:
			{
				if (account->flags & PF_SUPERADMIN)
				{
					// forcedly load player by login
					listener->push_text("Not implemented.");
				}
				else
				{
					listener->push_text("Check your privilege.");
				}
				break;
			}
			case CMD_SET_NOTE:
			{
				if (account->flags & PF_SUPERADMIN)
				{
					// set new greeting message
					listener->push_text("Not implemented.");
				}
				else
				{
					listener->push_text("Check your privilege.");
				}
				break;
			}
			case CMD_ADD_BAN:
			{
				if (account->flags & PF_SUPERADMIN)
				{
					// ban ip address
					listener->push_text("Not implemented.");
				}
				else
				{
					listener->push_text("Check your privilege.");
				}
				break;
			}
			case CMD_CREATE_GAME:
			{
				listener->push_text("Not implemented.");
				break;
			}
			case CMD_DELETE_GAME:
			{
				listener->push_text("Not implemented.");
				break;
			}
			case CMD_GAME_OWNER:
			{
				listener->push_text("Not implemented.");
				break;
			}
			case CMD_ADD_PLAYER:
			{
				listener->push_text("Not implemented.");
				break;
			}
			case CMD_DELETE_PLAYER:
			{
				listener->push_text("Not implemented.");
				break;
			}
			case CMD_JOIN_GAME:
			{
				listener->push_text("Not implemented.");
				break;
			}
			default:
			{
				listener->push_text(fmt::format("Invalid command (cmd={:#x}, v0={:#x}, v1={:#x}, v2={:#x})", cmd.cmd, cmd.v0, cmd.v1, cmd.v2));
			}
			}
		}
		else if (header.code == CLIENT_SCMD && header.size == 2)
		{
			u16 scmd;
			if (!socket->get(scmd))
			{
				return;
			}

			switch (scmd)
			{
			case SCMD_NONE: break;

			case SCMD_UPDATE_SERVER:
			{
				if (account->flags & PF_SUPERADMIN)
				{
					// restart server (TODO)
					stop(0);
				}
				else
				{
					listener->push_text("Check your privilege.");
				}
				break;
			}
			case SCMD_HIDE:
			{
				set_offline();
				std::this_thread::sleep_for(std::chrono::milliseconds(300));
				break;
			}
			case SCMD_SHOW:
			{
				set_online();
				std::this_thread::sleep_for(std::chrono::milliseconds(300));
				break;
			}
			case SCMD_REFRESH:
			{
				// Update player list (it shouldn't be necessary to use it)
				listener->push_packet(g_players.generate_player_list(player->index, std::unique_lock<account_list_t>(g_accounts)));

				std::this_thread::sleep_for(std::chrono::seconds(1));
				break;
			}
			case SCMD_QUIT:
			{
				// Quit manually

				if (account->flags & PF_LOCK)
				{
					listener->push_text("You cannot quit now.");
					return;
				}

				listener->push_text("You have quit.");
				listener->quit_flag.test_and_set();
				return;
			}

			default:
			{
				listener->push_text(fmt::format("Invalid command (scmd={:#x})", scmd));
			}
			}
		}
		else
		{
			listener->push_text(fmt::format("Invalid command (code={:#x}, size={})", +header.code, header.size));

			if (!socket->get(&cmd, header.size))
			{
				return;
			}
		}
	}
}

void sender_thread(std::shared_ptr<socket_t> socket, inaddr_t ip, u16 port)
{
	auto message = [](socket_t& socket, const char* text)
	{
		const packet_t& packet = ServerTextRec::make(GetTime(), text, strlen(text));

		socket.put(packet->data(), packet->size);
	};

	ProtocolHeader header;

	// send auth packet and receive header
	if (!socket->put(g_auth_packet->data(), g_auth_packet->size) || !socket->get(header))
	{
		ep_printf_ip("- (AUTH-1)\n", ip, port);
		return;
	}

	std::shared_ptr<account_t> account;

	{
		packet_t auth_info;

		// validate auth packet content
		if ((g_key_size != 0 || header.code != CLIENT_AUTH || header.size != sizeof(ClientAuthRec)) &&
			(g_key_size == 0 || header.code != CLIENT_SECURE_AUTH || header.size != g_key_size) ||
			(auth_info.reset(header.size), !socket->get(auth_info->data(), header.size)))
		{
			ep_printf_ip("- (AUTH-2) ({}, {})\n", ip, port, +header.code, header.size);
			message(*socket, "Handshake failed.");
			socket->put(ProtocolHeader{ SERVER_NONFATALDISCONNECT });
			return;
		}

		// select auth mode
		if (header.code == CLIENT_SECURE_AUTH)
		{
			mpz_class num;

			for (u32 i = 0; i < g_key_size; i++) // convert from base 256
			{
				num <<= 8;
				num += auth_info->get<u8>(i);
			}

			mpz_powm(num.get_mpz_t(), num.get_mpz_t(), g_key_d.get_mpz_t(), g_key_n.get_mpz_t()); // decrypt

			for (u32 i = g_key_size - 1; ~i; i--) // get decrypted data
			{
				auth_info->get<u8>(i) = static_cast<u8>(num.get_ui());
				num >>= 8;

				if (num == 0)
				{
					// fix data displacement (allocate new block)
					auth_info = { &auth_info->get(i), g_key_size - i };
					break;
				}
			}

			if (auth_info->size < sizeof(SecureAuthRec))
			{
				// clear invalid data (proceed with empty login)
				std::memset(auth_info->data(), 0, auth_info->size);
			}
			else
			{
				// re-initialize with encryption
				socket = std::make_shared<cipher_socket_t>(socket->release(), packet_t{ auth_info->get<SecureAuthRec>().ckey, 32 });
			}
		}

		auto& auth = auth_info->get<ClientAuthRec>();

		// check login
		if (auth.name.size() > 16 || !IsLoginValid(auth.name.data(), auth.name.size()))
		{
			ep_printf_ip("- (AUTH-3) ({})\n", ip, port, auth.name.size());
			message(*socket, "Invalid login.");
			socket->put(ProtocolHeader{ SERVER_DISCONNECT });
			return;
		}

		// prepare password
		HL_MD5_CTX ctx;
		MD5().MD5Init(&ctx);
		MD5().MD5Update(&ctx, auth.pass.data(), 16); // calculate md5 from md5(password) arrived
		MD5().MD5Final(auth.pass.data(), &ctx);

		ep_printf_ip("* LOGIN: {}\n", ip, port, auth.name.operator std::string());

		// find or create account
		if (!(account = g_accounts.add_account(auth.name, auth.pass)))
		{
			ep_printf_ip("- (AUTH-4)\n", ip, port);
			message(*socket, "Invalid password.");
			socket->put(ProtocolHeader{ SERVER_DISCONNECT });
			return;
		}
	}

	if (account->flags & PF_NOCONNECT)
	{
		ep_printf_ip("- (AUTH-5)\n", ip, port);
		message(*socket, "Account is banned.");
		socket->put(ProtocolHeader{ SERVER_DISCONNECT });
		return;
	}

	auto player = g_players.add_player(account);

	if (!player)
	{
		ep_printf_ip("- (AUTH-6)\n", ip, port);
		message(*socket, "Too many players connected.");
		socket->put(ProtocolHeader{ SERVER_DISCONNECT });
		return;
	}

	auto listener = std::make_shared<listener_t>(ip.s_addr, port, header.code == CLIENT_SECURE_AUTH);

	if (!player->add_listener(listener))
	{
		ep_printf_ip("- (AUTH-7)\n", ip, port);
		message(*socket, "Too many connections.");
		socket->put(ProtocolHeader{ SERVER_DISCONNECT });
		return;
	}

	// send version information
	listener->push(version_info);

	listener->push_text("EPServer git version: " GIT_VERSION); // TODO: print greeting and something else

	{
		std::unique_lock<account_list_t> acc_lock(g_accounts);

		// send player list
		listener->push_packet(g_players.generate_player_list(player->index, acc_lock));

		if (account->flags.fetch_and(~PF_NEW_PLAYER) & PF_NEW_PLAYER) // new player connected
		{
			g_players.update_player(player, acc_lock);
			g_players.broadcast(account->get_name(acc_lock) + "%/ connected as a new player.", only_online);
			g_accounts.save(acc_lock);
		}
		else if (account->flags.fetch_and(~PF_LOST) & PF_LOST) // connection restored
		{
			g_players.update_player(player, acc_lock);
			g_players.broadcast(account->get_name(acc_lock) + "%/ connected.", only_online);
		}
		else
		{
			g_players.update_player(player, acc_lock); // silent reconnection
		}
	}

	// start receiver subthread (it shouldn't send data directly)
	std::thread(receiver_thread, socket, account, player, listener).detach();

	// start sending packets
	while (packet_t packet{ listener->pop(30000, g_keepalive_packet) })
	{
		if (!socket->put(packet->data(), packet->size))
		{
			break;
		}
	}

	// detect connection lost
	if (player->remove_listener(listener) == PS_CONNECTION_LOST)
	{
		std::unique_lock<account_list_t> acc_lock(g_accounts);

		// check if the quit command has been sent
		if (listener->quit_flag.test_and_set())
		{
			g_players.broadcast(account->get_name(acc_lock) + "%/ has quit.", only_online);
			g_players.update_player(player, acc_lock, true);
			g_players.remove_player(player->index);
		}
		else
		{
			g_players.update_player(player, acc_lock);
			g_players.broadcast(account->get_name(acc_lock) + "%/ lost connection with server.", only_online);
		}
	}

	// close connection
	socket->put(ProtocolHeader{ listener->stop_flag.test_and_set() ? SERVER_DISCONNECT : SERVER_NONFATALDISCONNECT });
	ep_printf_ip("-\n", ip, port);
}

void stop(int x)
{
	g_players.broadcast("Server stopped for reboot.");
	g_players.broadcast(packet_t{});

	std::this_thread::sleep_for(std::chrono::seconds(1));

	std::unique_lock<account_list_t> acc_lock(g_accounts);
	
	g_accounts.save(acc_lock);
	acc_lock.release(); // leave locked
	g_server.close();
}

void fault(int x)
{
	ep_printf("Access violation.\n");

	g_accounts.lock(); // leave locked
	std::terminate();
}

int main(int arg_count, const char* args[])
{
	ep_printf("EPServer started.\n");

	if (std::signal(SIGINT, stop) == SIG_ERR)
	{
		ep_printf("signal(SIGINT) failed!\n");
		return 0;
	}

	if (std::signal(SIGSEGV, fault) == SIG_ERR)
	{
		ep_printf("signal(SIGSEGV) failed!\n");
		return 0;
	}

	fmt::print("EPServer git version: {}\n", GIT_VERSION);
	fmt::print("EPServer client version: {}\n", EP_VERSION);

	fmt::print("ipv4.dat not loaded!\n"); // TODO: load IP db
	fmt::print("ipv6.dat not loaded!\n"); // TODO: IPv6 support

	g_accounts.load(); // load account info

	fmt::print("accounts: {}\n", g_accounts.size());

	if (unique_FILE f{ std::fopen("key.dat", "rb") })
	{
		// get file size
		std::fseek(f.get(), 0, SEEK_END);
		const u32 size = std::ftell(f.get());

		// get file content
		packet_t keys{ size + 1 };
		std::fseek(f.get(), 0, SEEK_SET);
		std::fread(keys->data(), 1, size, f.get());

		// data pointer
		const auto ptr = &keys->get();
		
		// string pointers
		std::vector<char*> strings = { ptr };

		// add null character
		ptr[size] = 0;

		// make null-terminated strings in buffer
		for (u32 i = 0; i < size; i++)
		{
			if (!isdigit(ptr[i]))
			{
				ptr[i] = 0;

				if (isdigit(ptr[i + 1]))
				{
					strings.emplace_back(ptr + i + 1);
				}
			}
		}

		if (strings.size() != 5)
		{
			printf("key.dat is invalid!\n");
		}
		else
		{
			// load values
			mpz_class key_e(strings[0], 10);
			mpz_class key_p(strings[1], 10);
			mpz_class key_q(strings[2], 10);
			mpz_class key_n(strings[3], 10);
			mpz_class key_s(strings[4], 10);

			// set open key
			g_key_n = key_n;

			// calculate priv key
			g_key_d = (key_p - 1) * (key_q - 1);
			mpz_invert(g_key_d.get_mpz_t(), key_e.get_mpz_t(), g_key_d.get_mpz_t());

			// set rough key size
			g_key_size = static_cast<u32>(mpz_size(g_key_d.get_mpz_t()) * sizeof(mp_limb_t));

			// prepare auth packet
			g_auth_packet.reset(3 + g_key_size * 2);
			g_auth_packet->get<ProtocolHeader>() = { SERVER_AUTH, static_cast<u16>(g_key_size * 2) };

			// convert to base 256
			for (u32 i = g_key_size - 1; ~i; i--)
			{
				g_auth_packet->get<u8>(i + 3) = static_cast<u8>(key_n.get_ui());
				g_auth_packet->get<u8>(i + g_key_size + 3) = static_cast<u8>(key_s.get_ui());
				key_n >>= 8;
				key_s >>= 8;
			}
		}
	}
	else
	{
		fmt::print("key.dat not found!\n");
	}

	fmt::print("key size: {}\n", g_key_size * 8);

	if (g_auth_packet->size == 0)
	{
		g_auth_packet.reset(3);
		g_auth_packet->get<ProtocolHeader>() = { SERVER_AUTH };
	}

	g_keepalive_packet.reset(5);
	g_keepalive_packet->get<ClientSCmdRec>() = { { CLIENT_SCMD, 2 }, SCMD_NONE };

#ifdef _WIN32
	WSADATA wsa_info{};

	if (auto res = WSAStartup(MAKEWORD(2, 2), &wsa_info))
	{
		fmt::print("WSAStartup() failed: {:#x}\n", res);
		return -1;
	}

	atexit([]()
	{
		WSACleanup();
	});
#endif

	socket_id_t sid = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sid == INVALID_SOCKET)
	{
		fmt::print("socket() failed: {:#x}\n", GETERROR);
		return -1;
	}

	g_server.reset(sid);

	sockaddr_in info;
	info.sin_family = AF_INET;
	info.sin_addr.s_addr = INADDR_ANY;
	info.sin_port = htons(4044);

	if (bind(sid, reinterpret_cast<sockaddr*>(&info), sizeof(sockaddr_in)) == SOCKET_ERROR)
	{
		fmt::print("bind() failed: {:#x}\n", GETERROR);
		return -1;
	}

	if (listen(sid, SOMAXCONN) == SOCKET_ERROR)
	{
		fmt::print("listen() failed: {:#x}\n", GETERROR);
		return -1;
	}

	while (true)
	{
		// accept connection
		socklen_t size = sizeof(sockaddr_in);
		socket_id_t aid = accept(sid, reinterpret_cast<sockaddr*>(&info), &size);

		if (aid == INVALID_SOCKET)
		{
			ep_printf("EPServer stopped.\n");
			return 0;
		}

		ep_printf_ip("+\n", info.sin_addr, info.sin_port);

		// start client thread
		std::thread(sender_thread, std::make_shared<socket_t>(aid), info.sin_addr, info.sin_port).detach();
	}
}
