#include "stdafx.h"
#include "ep_defines.h"
#include "ep_socket.h"
#include "ep_account.h"
#include "ep_player.h"
#include "ep_listener.h"
#include "hl_md5.h"

const ServerVersionRec version_info = { SERVER_VERSIONINFO, sizeof(ServerVersionRec) - 3, short_str_t<30>::make(vers, strlen(vers)) };

account_list_t g_accounts;
player_list_t g_players;
listener_list_t g_listeners;
socket_t g_server;

packet_data_t g_auth_packet(3);
packet_data_t g_priv_key;

void receiver_thread(std::shared_ptr<socket_t> socket, std::shared_ptr<account_t> account, std::shared_ptr<player_t> player, std::shared_ptr<listener_t> listener)
{
	auto only_online_players = [](listener_t& l){ return (l.player->account->flags & PF_OFF) == 0; };
	auto all_players = [](listener_t& l){ return true; };

	std::unique_ptr<listener_t, void(*)(listener_t*)> finalizer(listener.get(), [](listener_t* listener)
	{
		// execute at thread exit
		listener->stop();
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(800));

	while (true)
	{
		socket->flush();

		ProtocolHeader header;
		if (!socket->get(header))
		{
			return;
		}

		// TODO: reset last activity time

		if (header.code == CLIENT_CMD && header.size >= 14)
		{
			ClientCmdRec cmd;
			if (!socket->get(&cmd, header.size))
			{
				return;
			}

			size_t text_size = header.size - 14;

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
				}
				else
				{
					listener->push_text("Invalid command (CMD_CHAT, v0=" + std::to_string(cmd.v0) + ", v1=" + std::to_string(cmd.v1) + ", v2=" + std::to_string(cmd.v2) + ")\n" + message);
				}

				// ~114 ms + 2 ms per character
				std::this_thread::sleep_for(std::chrono::milliseconds(100 + header.size * 2));
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
				}
				else
				{
					listener->push_text("Invalid command (CMD_DICE, v0=" + std::to_string(cmd.v0) + ", v1=" + std::to_string(cmd.v1) + ", v2=" + std::to_string(cmd.v2) + ")");
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(300));
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
					account->email = short_str_t<>::make(cmd.data, text_size);
					listener->push_text("E-mail set: " + std::string(account->email.data, account->email.length));

					g_accounts.save();
					std::this_thread::sleep_for(std::chrono::milliseconds(1000));
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
				if ((account->flags & PF_SUPERADMIN) && (1ull << cmd.v1 != PF_SUPERADMIN))
				{
					// find cmd.v0 player and change flag
					listener->push_text("Not implemented.");
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
		else if (header.code == CLIENT_SCMD && header.size == sizeof(u16))
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
					// restart server
					listener->push_text("Not implemented.");
					g_server.close(); // for test
				}
				else
				{
					listener->push_text("Check your privilege.");
				}
				break;
			}
			case SCMD_HIDE:
			{
				if ((account->flags.fetch_or(PF_OFF) & PF_OFF) == 0)
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
				listener->push_packet(packet_t(new packet_data_t(std::move(g_players.generate_player_list(player->index)))));

				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
				break;
			}
			case SCMD_QUIT:
			{
				// Quit manually (TODO)
				//g_players.remove_player(player->index);
				//g_listeners.update_player(player);
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

			u8 raw[65535];
			if (!socket->get(raw, header.size))
			{
				return;
			}
		}
	}
}

void sender_thread(socket_id_t aid, inaddr_t ip, u16 port)
{
	auto message = [](socket_t& socket, std::string text)
	{
		const u16 size = static_cast<u16>(std::min<size_t>(text.size(), ServerTextRec::max_data_size));

		ServerTextRec data = { SERVER_TEXT, static_cast<u16>(size + sizeof(double)), GetTime() };
		memcpy(data.data, text.c_str(), size);

		socket.put(&data, data.header.size + 3);
	};

	if (send(aid, g_auth_packet.get(), g_auth_packet.size(), 0) != g_auth_packet.size())
	{
		socket_t socket(aid);
		printf("- %s:%d (I)\n", inet_ntoa(ip), port);
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
			(header.code != CLIENT_AUTH || header.size != sizeof(ClientAuthRec) - 3) &&
			(g_priv_key.size() == 0 || header.code != CLIENT_SECURE_AUTH || header.size != g_priv_key.size()) ||
			(auth_info.reset(header.size), recv(aid, auth_info.get(), header.size, MSG_WAITALL) != header.size))
		{
			socket_t socket(aid);
			printf("- %s:%d (II)\n", inet_ntoa(ip), port);
			message(socket, "Invalid auth packet");
			socket.put(ProtocolHeader{ SERVER_NONFATALDISCONNECT });
			return;
		}

		// select auth mode
		if (header.code == CLIENT_SECURE_AUTH)
		{
			packet_data_t key(32);

			// TODO

			socket.reset(new cipher_socket_t(aid, std::move(key)));
		}
		else
		{
			socket.reset(new socket_t(aid)); // simple socket
		}

		auto auth = auth_info.get<ClientAuthRec>();

		// check login
		if (auth->name.length > 16 || !IsLoginValid(auth->name.data, auth->name.length))
		{
			printf("- %s:%d (III)\n", inet_ntoa(ip), port);
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
			printf("- %s:%d (IV)\n", inet_ntoa(ip), port); // TODO: messages (wrong password)
			message(*socket, "Invalid password");
			socket->put(ProtocolHeader{ SERVER_DISCONNECT });
			return;
		}
	}

	if (account->flags & PF_NOCONNECT)
	{
		printf("- %s:%d (V)\n", inet_ntoa(ip), port);
		message(*socket, "Account is banned");
		socket->put(ProtocolHeader{ SERVER_DISCONNECT });
		return;
	}

	g_accounts.save(); // TODO: remove

	std::shared_ptr<player_t> player = g_players.add_player(account);
	if (!player)
	{
		printf("- %s:%d (VI)\n", inet_ntoa(ip), port);
		message(*socket, "Too many players connected");
		socket->put(ProtocolHeader{ SERVER_NONFATALDISCONNECT });
		return;
	}

	std::shared_ptr<listener_t> listener = g_listeners.add_listener(player);
	if (!listener)
	{
		printf("- %s:%d (VII)\n", inet_ntoa(ip), port);
		message(*socket, "Too many connections");
		socket->put(ProtocolHeader{ SERVER_NONFATALDISCONNECT });
		return;
	}

	std::unique_ptr<listener_t, void(*)(listener_t*)> finalizer(listener.get(), [](listener_t* listener)
	{
		// execute at thread exit
		g_listeners.remove_listener(listener);
	});

	account->flags &= ~PF_LOST; // remove PF_LOST flag (TODO)

	{
		const packet_data_t plist = g_players.generate_player_list(player->index);

		if (!socket->put(version_info) || !socket->put(plist.get(), plist.size()))
		{
			printf("- %s:%d (VIII)\n", inet_ntoa(ip), port);
			socket->put(ProtocolHeader{ SERVER_NONFATALDISCONNECT });
			return;
		}
	}

	g_listeners.update_player(player);

	// start receiver subthread (it shouldn't send data directly)
	std::thread(receiver_thread, socket, account, player, listener).detach();

	// start sending packets
	while (auto packet = listener->pop())
	{
		if (!socket->put(packet->get<void>(), packet->size()))
		{
			printf("- %s:%d (IX)\n", inet_ntoa(ip), port);
			socket->put(ProtocolHeader{ SERVER_NONFATALDISCONNECT });
			return;
		}
	}

	// connection closed
	printf("- %s:%d (X)\n", inet_ntoa(ip), port);
	socket->put(ProtocolHeader{ SERVER_DISCONNECT });
}

int main()
{
	printf("EPServer version: '%s'\n", vers);

	printf("ipv4.dat not loaded!\n"); // TODO: load IP db
	printf("ipv6.dat not loaded!\n"); // TODO: IPv6 support

	g_accounts.load(); // load account info

	printf("accounts: %d\n", g_accounts.size());

	if (auto f = std::fopen("auth.dat", "r"))
	{
		std::fseek(f, 0, SEEK_END);
		const u32 size = std::ftell(f);

		g_auth_packet.reset(3 + size);
		*g_auth_packet.get<ProtocolHeader>() = { SERVER_AUTH, static_cast<u16>(size) };

		std::fseek(f, 0, SEEK_SET);
		std::fread(g_auth_packet.get() + 3, 1, size, f);

		std::fclose(f);

		printf("auth.dat size: %d\n", size);
	}
	else
	{
		g_auth_packet.reset(3);
		*g_auth_packet.get<ProtocolHeader>() = { SERVER_AUTH };

		printf("auth.dat not found!\n");
	}

	if (auto f = std::fopen("key.dat", "r"))
	{
		std::fseek(f, 0, SEEK_END);
		const u32 size = std::ftell(f);

		g_priv_key.reset(size);

		std::fseek(f, 0, SEEK_SET);
		std::fread(g_auth_packet.get(), 1, size, f);

		std::fclose(f);

		printf("key.dat size: %d\n", size);
	}
	else
	{
		printf("key.dat not found!\n");
	}

#ifdef _WIN32
	WSADATA wsa_info = {};

	if (auto res = WSAStartup(MAKEWORD(2, 2), &wsa_info))
	{
		printf("WSAStartup() failed: 0x%x\n", res);
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
		printf("socket() failed: 0x%x\n", GETERROR);
		return -1;
	}

	g_server.reset(sid);

	sockaddr_in info;
	info.sin_family = AF_INET;
	info.sin_addr.s_addr = INADDR_ANY;
	info.sin_port = htons(4044);

	if (bind(sid, (sockaddr*)&info, sizeof(sockaddr_in)) == SOCKET_ERROR)
	{
		printf("bind() failed: 0x%x\n", GETERROR);
		return -1;
	}

	if (listen(sid, SOMAXCONN) == SOCKET_ERROR)
	{
		printf("listen() failed: 0x%x\n", GETERROR);
		return -1;
	}

	while (true)
	{
		// accept connection
		socklen_t size = sizeof(sockaddr_in);
		socket_id_t aid = accept(sid, (sockaddr*)&info, &size);
		if (aid == INVALID_SOCKET)
		{
			printf("accept() returned 0x%x\n", GETERROR);
			printf("EPServer stopped.\n");
			return 0;
		}

		printf("+ %s:%d\n", inet_ntoa(info.sin_addr), info.sin_port);

		// start client thread
		std::thread(sender_thread, aid, info.sin_addr, info.sin_port).detach();
	}
}
