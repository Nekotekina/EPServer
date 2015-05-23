#include "stdafx.h"
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

const ServerVersionRec version_info = { SERVER_VERSIONINFO, sizeof(ServerVersionRec) - 3, short_str_t<30>::make(ep_version, strlen(ep_version)) };

account_list_t g_accounts;
player_list_t g_players;
listener_list_t g_listeners;
socket_t g_server;

void stop(int x);

packet_t g_keepalive_packet;

packet_data_t g_auth_packet; // open key + sign
mpz_class g_key_n; // open key
mpz_class g_key_d; // priv key
u32 g_key_size = 0; // key size (bytes)

bool only_online_players(listener_t& listener)
{
	return (listener.player->account->flags & PF_OFF) == 0;
}

bool all_players(listener_t& listener)
{
	return true;
}

void receiver_thread(std::shared_ptr<socket_t> socket, std::shared_ptr<account_t> account, std::shared_ptr<player_t> player, std::shared_ptr<listener_t> listener)
{
	std::unique_ptr<listener_t, void(*)(listener_t*)> listener_stopper(listener.get(), [](listener_t* listener) // scope exit
	{
		listener->stop();
	});

	std::this_thread::sleep_for(std::chrono::seconds(1));

	ProtocolHeader header;
	ClientCmdRec cmd;

	while (socket->flush(), socket->get(header))
	{
		// TODO: reset last activity time

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
				std::string message(cmd.data, text_size);

				if (message.find("%p") != std::string::npos)
				{
					listener->push_text("You cannot send %%p marker.\n" + message);
				}
				else if (message.find("%/") != std::string::npos)
				{
					listener->push_text("You cannot send %%/ marker.\n" + message);
				}
				else if (cmd.v0 == -1 && !cmd.v1 && !cmd.v2)
				{
					// public message
					if (account->flags & PF_NOCHAT)
					{
						listener->push_text("You cannot write public messages.\n" + message);
					}
					else
					{
						if (account->flags.fetch_and(~PF_OFF) & PF_OFF)
						{
							g_listeners.broadcast(account->get_name() + "%/ is online.", only_online_players);
							g_listeners.update_player(player);
						}

						g_listeners.broadcast(account->get_name() + "%/ %bwrites:%x " + message + "%x", only_online_players);
					}

					// ~207 ms + 1 ms per character
					std::this_thread::sleep_for(std::chrono::milliseconds(200 + header.size));
				}
				else if (cmd.v0 >= 0 && !cmd.v1 && !cmd.v2)
				{
					// private message
					if (account->flags & PF_NOPRIVCHAT)
					{
						listener->push_text("You cannot write private messages.\n" + message);
					}
					else
					{
						g_listeners.broadcast(account->get_name() + "%/%p%g writes (private):%x " + message + "%x", [&](listener_t& l){ return l.player->index == cmd.v0; });
					}

					// ~201 ms + 0.25 ms per character
					std::this_thread::sleep_for(std::chrono::milliseconds(200 + header.size / 4));
				}
				else
				{
					listener->push_text("Invalid command (CMD_CHAT, v0=" + std::to_string(cmd.v0) + ", v1=" + std::to_string(cmd.v1) + ", v2=" + std::to_string(cmd.v2) + ")\n" + message);
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
						if (account->flags.fetch_and(~PF_OFF) & PF_OFF)
						{
							g_listeners.broadcast(account->get_name() + "%/ is online.", only_online_players);
							g_listeners.update_player(player);
						}

						g_listeners.broadcast(account->get_name() + "%/ throws " + FormatDice(cmd.v1), only_online_players);
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
					else
					{
						std::string dice = FormatDice(cmd.v1);

						g_listeners.broadcast(account->get_name() + "%/%p throws " + dice + " to you (private)", [&](listener_t& l){ return l.player->index == cmd.v0; });
						listener->push_text("You throw " + dice + "%/ to " + g_players.get_name_by_index(cmd.v0));
					}

					// 200 ms
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
				}
				else
				{
					listener->push_text("Invalid command (CMD_DICE, v0=" + std::to_string(cmd.v0) + ", v1=" + std::to_string(cmd.v1) + ", v2=" + std::to_string(cmd.v2) + ")");
				}

				break;
			}
			case CMD_SHOUT:
			{
				if (account->flags & PF_SUPERADMIN)
				{
					std::string message(cmd.data, text_size);

					g_listeners.broadcast(account->get_name() + "%/ %bwrites:%x " + message + "%x", all_players);
				}
				else
				{
					listener->push_text("Check your privilege.");
				}
				break;
			}
			case CMD_SET_EMAIL:
			{
				if (text_size > 255)
				{
					listener->push_text("Invalid e-mail.");
				}
				else if (cmd.v0 == -1 && !cmd.v1 && !cmd.v2)
				{
					account->email = short_str_t<255>::make(cmd.data, text_size);
					listener->push_text("E-mail set:");
					listener->push_text(account->email);

					g_accounts.save();
					std::this_thread::sleep_for(std::chrono::seconds(1));
				}
				else if (account->flags & PF_SUPERADMIN)
				{
					// find cmd.v0 player and set email
					listener->push_text("Not implemented.");
				}
				else
				{
					listener->push_text("Check your privilege.");
				}
				break;
			}
			case CMD_SET_PASSWORD:
			{
				if (text_size < 16)
				{
					listener->push_text("Invalid password.");
				}
				else if (cmd.v0 == -1 && !cmd.v1 && !cmd.v2)
				{
					// check old password and set new one
					listener->push_text("Not implemented.");
				}
				else if (account->flags & PF_SUPERADMIN)
				{
					// find cmd.v0 player and set password
					listener->push_text("Not implemented.");
				}
				else
				{
					listener->push_text("Check your privilege.");
				}
				break;
			}
			case CMD_SET_FLAG:
			{
				if (account->flags & PF_SUPERADMIN)
				{
					if (cmd.v0 >= 0 && cmd.v1 < 64u && (1ull << cmd.v1 != PF_SUPERADMIN) && !cmd.v2)
					{
						// find cmd.v0 player and change flag
						listener->push_text("Not implemented.");
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
					listener->push_text("Not implemented.");
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
					listener->push_text("Not implemented.");
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
					//
					listener->push_text("Not implemented.");
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
					listener->push_text("Not implemented.");
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
				listener->push_text("Invalid command (CMD " + std::to_string(cmd.cmd) + ", v0=" + std::to_string(cmd.v0) + ", v1=" + std::to_string(cmd.v1) + ", v2=" + std::to_string(cmd.v2) + ")");
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
				if (~account->flags.fetch_or(PF_OFF) & PF_OFF)
				{
					const s32 index = player->index;
					g_listeners.broadcast(account->get_name() + "%/ is offline.", [=](listener_t& l){ return (l.player->account->flags & PF_OFF) == 0 || l.player->index == index; });
					g_listeners.update_player(player);
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(300));
				break;
			}
			case SCMD_SHOW:
			{
				if (account->flags.fetch_and(~PF_OFF) & PF_OFF)
				{
					g_listeners.broadcast(account->get_name() + "%/ is online.", only_online_players);
					g_listeners.update_player(player);
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(300));
				break;
			}
			case SCMD_REFRESH:
			{
				// Update player list (isn't really used)
				listener->push_packet(packet_t(new packet_data_t(g_players.generate_player_list(player->index))));

				std::this_thread::sleep_for(std::chrono::seconds(1));
				break;
			}
			case SCMD_QUIT:
			{
				// Quit manually

				if (account->flags & PF_LOCK)
				{
					listener->push_text("You cannot quit now.");
					break;
				}

				listener->push_text("You have quit.");
				listener->quit_flag.test_and_set();
				return;
			}

			default:
			case SCMD_TIMEOUT_QUIT: // obsolete
			{
				listener->push_text("Invalid command (SCMD " + std::to_string(scmd) + ")");
			}
			}
		}
		else
		{
			listener->push_text("Invalid command (code=" + std::to_string(header.code) + ", size=" + std::to_string(header.size) + ")");

			if (!socket->get(&cmd, header.size))
			{
				return;
			}
		}
	}
}

void sender_thread(socket_id_t aid, inaddr_t ip, u16 port)
{
	auto message = [](socket_t& socket, const char* text)
	{
		packet_data_t packet = ServerTextRec::generate(GetTime(), text, strlen(text));

		socket.put(packet.get(), packet.size());
	};

	if (send(aid, g_auth_packet.get(), g_auth_packet.size(), 0) != g_auth_packet.size())
	{
		socket_t socket(aid);
		std::printf("- %s:%d (I)\n", inet_ntoa(ip), port);
		socket.put(ProtocolHeader{ SERVER_NONFATALDISCONNECT });
		return;
	}

	std::shared_ptr<socket_t> socket;

	std::shared_ptr<account_t> account;

	{
		packet_data_t auth_info;

		ProtocolHeader header;

		// validate auth packet content
		if (recv(aid, (char*)&header, 3, MSG_WAITALL) != 3 ||
			(header.code != CLIENT_AUTH || header.size != sizeof(ClientAuthRec)) &&
			(g_key_size == 0 || header.code != CLIENT_SECURE_AUTH || header.size != g_key_size) ||
			(auth_info.reset(header.size), recv(aid, auth_info.get(), header.size, MSG_WAITALL) != header.size))
		{
			socket_t socket(aid);
			std::printf("- %s:%d (II)\n", inet_ntoa(ip), port);
			message(socket, "Invalid auth packet");
			socket.put(ProtocolHeader{ SERVER_NONFATALDISCONNECT });
			return;
		}

		// select auth mode
		if (header.code == CLIENT_SECURE_AUTH)
		{
			mpz_class num;

			for (u32 i = 0; i < g_key_size; i++) // convert from base 256
			{
				num <<= 8;
				num += auth_info.get<u8>()[i];
			}

			mpz_powm(num.get_mpz_t(), num.get_mpz_t(), g_key_d.get_mpz_t(), g_key_n.get_mpz_t()); // decrypt

			for (u32 i = g_key_size - 1; ~i; i--) // get decrypted data
			{
				auth_info.get<u8>()[i] = static_cast<u8>(num.get_ui());
				num >>= 8;
				
				if (num == 0)
				{
					// fix data displacement (allocate new block)
					packet_data_t new_info(g_key_size - i);
					std::memcpy(new_info.get(), auth_info.get() + i, new_info.size());
					auth_info = std::move(new_info);
					break;
				}
			}

			if (auth_info.size() < sizeof(SecureAuthRec))
			{
				// clear invalid data (proceed with empty login)
				std::memset(auth_info.get(), 0, auth_info.size());
			}
			else
			{
				packet_data_t key(32);

				// copy session key
				memcpy(key.get(), auth_info.get<SecureAuthRec>()->ckey, key.size());

				// initialize socket with encryption
				socket.reset(new cipher_socket_t(aid, std::move(key)));
			}
		}
		
		if (!socket)
		{
			// initialize socket without encryption
			socket.reset(new socket_t(aid));
		}

		auto auth = auth_info.get<ClientAuthRec>();

		// check login
		if (auth->name.length > 16 || !IsLoginValid(auth->name.data, auth->name.length))
		{
			std::printf("- %s:%d (III)\n", inet_ntoa(ip), port);
			message(*socket, "Invalid login");
			socket->put(ProtocolHeader{ SERVER_DISCONNECT });
			return;
		}

		// prepare password
		HL_MD5_CTX ctx;
		MD5().MD5Init(&ctx);
		MD5().MD5Update(&ctx, auth->pass.data(), 16); // calculate md5 from md5(password) arrived
		MD5().MD5Final(auth->pass.data(), &ctx);

		// find or create account
		if (!(account = g_accounts.add_account(auth->name, auth->pass)))
		{
			std::printf("- %s:%d (IV)\n", inet_ntoa(ip), port); // TODO: messages (wrong password)
			message(*socket, "Invalid password");
			socket->put(ProtocolHeader{ SERVER_DISCONNECT });
			return;
		}
	}

	if (account->flags & PF_NOCONNECT)
	{
		std::printf("- %s:%d (V)\n", inet_ntoa(ip), port);
		message(*socket, "Account is banned");
		socket->put(ProtocolHeader{ SERVER_DISCONNECT });
		return;
	}

	g_accounts.save(); // TODO: remove

	std::shared_ptr<player_t> player = g_players.add_player(account);

	if (!player)
	{
		std::printf("- %s:%d (VI)\n", inet_ntoa(ip), port);
		message(*socket, "Too many players connected");
		socket->put(ProtocolHeader{ SERVER_NONFATALDISCONNECT });
		return;
	}

	std::shared_ptr<listener_t> listener = g_listeners.add_listener(player);

	if (!listener)
	{
		std::printf("- %s:%d (VII)\n", inet_ntoa(ip), port);
		message(*socket, "Too many connections");
		socket->put(ProtocolHeader{ SERVER_NONFATALDISCONNECT });
		return;
	}

	std::unique_ptr<listener_t, void(*)(listener_t*)> listener_remover(listener.get(), [](listener_t* listener) // scope exit
	{
		if (g_listeners.remove_listener(listener) == 0 && ~listener->player->account->flags.fetch_or(PF_LOST) & PF_LOST)
		{
			// connection lost

			// check if the quit command has been sent
			if (listener->quit_flag.test_and_set())
			{
				g_listeners.broadcast(listener->player->account->get_name() + "%/ has quit.", only_online_players);
				g_players.remove_player(listener->player->index);
				g_listeners.update_player(listener->player, true);
			}
			else
			{
				g_listeners.update_player(listener->player);
			}
		}
	});

	{
		const packet_data_t plist = g_players.generate_player_list(player->index);

		if (!socket->put(version_info) || !socket->put(plist.get(), plist.size()))
		{
			std::printf("- %s:%d (VIII)\n", inet_ntoa(ip), port);
			socket->put(ProtocolHeader{ SERVER_NONFATALDISCONNECT });
			return;
		}
	}

	listener->push_text("EPServer git version: " GIT_VERSION); // TODO: print greeting and something else

	if (account->flags.fetch_and(~PF_NEW_PLAYER) & PF_NEW_PLAYER) // new player connected
	{
		g_listeners.update_player(player);
		g_listeners.broadcast(account->get_name() + "%/ connected as a new player.", only_online_players);
	}

	if (account->flags.fetch_and(~PF_LOST) & PF_LOST) // connection restored
	{
		g_listeners.update_player(player);
		g_listeners.broadcast(account->get_name() + "%/ connected.", only_online_players);
	}

	g_listeners.update_player(player); // silent reconnection

	// start receiver subthread (it shouldn't send data directly)
	std::thread(receiver_thread, socket, account, player, listener).detach();

	// start sending packets
	while (auto packet = listener->pop(30000, g_keepalive_packet))
	{
		if (!socket->put(packet->get<void>(), packet->size()))
		{
			std::printf("- %s:%d (IX)\n", inet_ntoa(ip), port);
			socket->put(ProtocolHeader{ SERVER_NONFATALDISCONNECT });
			return;
		}
	}

	// connection closed
	std::printf("- %s:%d (X)\n", inet_ntoa(ip), port);
	socket->put(ProtocolHeader{ SERVER_DISCONNECT });
}

void stop(int x)
{
	g_listeners.broadcast("Server stopped for reboot.", [](listener_t&){ return true; });
	g_listeners.stop_all();

	std::this_thread::sleep_for(std::chrono::seconds(1));
	
	g_accounts.save();
	g_accounts.lock();
	g_server.close();
}

int main(int arg_count, const char* args[])
{
#ifdef __unix__
	//std::signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE (disabled)
#endif

	if (std::signal(SIGINT, stop) == SIG_ERR)
	{
		std::printf("signal(SIGINT) failed");
	}

	std::printf("EPServer git version: " GIT_VERSION "\n");
	std::printf("EPServer client version: '%s'\n", ep_version);

	std::printf("ipv4.dat not loaded!\n"); // TODO: load IP db
	std::printf("ipv6.dat not loaded!\n"); // TODO: IPv6 support

	g_accounts.load(); // load account info

	std::printf("accounts: %d\n", g_accounts.size());

	if (unique_FILE f{ std::fopen("key.dat", "rb") })
	{
		// get file size
		std::fseek(f.get(), 0, SEEK_END);
		const u32 size = std::ftell(f.get());

		// get file content
		packet_data_t keys(size + 1);
		std::fseek(f.get(), 0, SEEK_SET);
		std::fread(keys.get(), 1, size, f.get());

		// data pointer
		char* ptr = keys.get();
		
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
			*g_auth_packet.get<ProtocolHeader>() = { SERVER_AUTH, static_cast<u16>(g_key_size * 2) };

			// convert to base 256
			for (u32 i = g_key_size - 1; ~i; i--)
			{
				g_auth_packet.get<u8>()[i + 3] = static_cast<u8>(key_n.get_ui());
				g_auth_packet.get<u8>()[i + g_key_size + 3] = static_cast<u8>(key_s.get_ui());
				key_n >>= 8;
				key_s >>= 8;
			}
		}
	}
	else
	{
		std::printf("key.dat not found!\n");
	}

	std::printf("key size: %d\n", g_key_size * 8);

	if (g_auth_packet.size() == 0)
	{
		g_auth_packet.reset(3);
		*g_auth_packet.get<ProtocolHeader>() = { SERVER_AUTH };
	}

	g_keepalive_packet.reset(new packet_data_t(5));
	*g_keepalive_packet->get<ClientSCmdRec>() = { { CLIENT_SCMD, 2 }, SCMD_NONE };

#ifdef _WIN32
	WSADATA wsa_info = {};

	if (auto res = WSAStartup(MAKEWORD(2, 2), &wsa_info))
	{
		std::printf("WSAStartup() failed: 0x%x\n", res);
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
		std::printf("socket() failed: 0x%x\n", GETERROR);
		return -1;
	}

	g_server.reset(sid);

	sockaddr_in info;
	info.sin_family = AF_INET;
	info.sin_addr.s_addr = INADDR_ANY;
	info.sin_port = htons(4044);

	if (bind(sid, (sockaddr*)&info, sizeof(sockaddr_in)) == SOCKET_ERROR)
	{
		std::printf("bind() failed: 0x%x\n", GETERROR);
		return -1;
	}

	if (listen(sid, SOMAXCONN) == SOCKET_ERROR)
	{
		std::printf("listen() failed: 0x%x\n", GETERROR);
		return -1;
	}

	while (true)
	{
		// accept connection
		socklen_t size = sizeof(sockaddr_in);
		socket_id_t aid = accept(sid, (sockaddr*)&info, &size);

		if (aid == INVALID_SOCKET)
		{
			std::printf("EPServer stopped.\n");
			return 0;
		}

		std::printf("+ %s:%d\n", inet_ntoa(info.sin_addr), info.sin_port);

		// start client thread
		std::thread(sender_thread, aid, info.sin_addr, info.sin_port).detach();
	}
}
