#pragma once
#include "ep_defines.h"
#include "rc6.h"

#ifdef _WIN32

#if defined(_MSC_VER)
#define _WINSOCK_DEPRECATED_NO_WARNINGS // TODO
#pragma comment(lib, "ws2_32.lib")
#endif

#include <winsock2.h>
#define GETERROR WSAGetLastError()
#define DROP(sid) closesocket(sid)
using socket_id_t = SOCKET;
using inaddr_t = IN_ADDR;
using socklen_t = int;

#else

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define GETERROR errno
#define DROP(sid) ::close(sid)
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
using socket_id_t = int;
using inaddr_t = decltype(sockaddr_in::sin_addr);

#endif

class socket_t
{
protected:
	std::atomic<socket_id_t> socket;

public:
	socket_t()
		: socket(INVALID_SOCKET)
	{
	}

	socket_t(socket_id_t socket)
		: socket(socket)
	{
	}

	virtual ~socket_t()
	{
		if (socket != INVALID_SOCKET)
		{
			DROP(socket);
		}
	}

	virtual void reset(socket_id_t socket)
	{
		auto old_socket = this->socket.exchange(socket);

		if (old_socket != INVALID_SOCKET)
		{
			DROP(old_socket);
		}
	}

	void close()
	{
		reset(INVALID_SOCKET);
	}

	// send data
	virtual bool put(const void* data, u32 size)
	{
		return send(socket, reinterpret_cast<const char*>(data), size, 0) == size;
	}

	// send data
	template<typename T> bool put(const T& data)
	{
		return put(&data, sizeof(T));
	}

	// receive data
	virtual bool get(void* data, u32 size)
	{
		return recv(socket, reinterpret_cast<char*>(data), size, MSG_WAITALL) == size;
	}

	// receive data
	template<typename T> bool get(T& data)
	{
		return get(&data, sizeof(T));
	}

	// clear cipher padding in input buffer
	virtual void flush()
	{
	}
};

class server_socket_t : public socket_t
{
protected:

public:

};

class cipher_socket_t : public socket_t
{
protected:
	rc6_cipher_t m_cipher;

public:
	cipher_socket_t(packet_data_t key)
		: m_cipher(std::move(key))
	{
	}

	virtual bool put(const void* data, u32 size) override
	{
		const u32 asize = size + 15 & ~15;

		std::unique_ptr<rc6_block_t[]> buf(new rc6_block_t[asize % 16]);

		memset(buf.get(), 0, asize);

		memcpy(buf.get(), data, size);

		for (u32 i = 0; i < asize / 16; i++)
		{
			m_cipher.encrypt_cbc(buf.get()[i]);
		}

		return socket_t::put(buf.get(), asize);
	}

	virtual bool get(void* data, u32 size) override
	{
		return socket_t::get(data, size);
	}

	virtual void flush() override
	{
		return socket_t::flush();
	}
};

class web_socket_t : public socket_t
{
protected:

public:
	virtual bool put(const void* data, u32 size) override
	{
		return socket_t::put(data, size);
	}

	virtual bool get(void* data, u32 size) override
	{
		return socket_t::get(data, size);
	}

	virtual void flush() override
	{
		return socket_t::flush();
	}
};
