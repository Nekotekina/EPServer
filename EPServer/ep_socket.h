#pragma once
#include "ep_defines.h"
#include "rc6.h"

#ifdef _WIN32

#include <winsock2.h>
#define GETERROR WSAGetLastError()
#define DROP(sid) closesocket(sid)
#define MSG_NOSIGNAL 0
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

void print_time_ip(inaddr_t ip, u16 port);

// Print logs with current time and specified IP:port
template<typename... T> inline void ep_printf_ip(const char* fmt, inaddr_t ip, u16 port, T&&... args)
{
	print_time_ip(ip, port);
	std::printf(fmt, std::forward<T>(args)...);
}

inline void ep_printf_ip(const char* fmt, inaddr_t ip, u16 port)
{
	print_time_ip(ip, port);
	std::printf("%s", fmt);
}

class socket_t
{
protected:
	std::atomic<socket_id_t> m_socket;

public:
	socket_t()
		: m_socket(INVALID_SOCKET)
	{
	}

	socket_t(socket_id_t socket)
		: m_socket(socket)
	{
	}

	virtual ~socket_t()
	{
		if (m_socket != INVALID_SOCKET)
		{
			DROP(m_socket);
		}
	}

	socket_id_t release()
	{
		return m_socket.exchange(INVALID_SOCKET);
	}

	// close socket (only for server socket, temporarily)
	void close()
	{
		DROP(release());
	}

	// reset socket (only for server socket, temporarily)
	void reset(socket_id_t socket)
	{
		auto old_socket = m_socket.exchange(socket);

		if (old_socket != INVALID_SOCKET)
		{
			DROP(old_socket);
		}
	}

	// send data
	virtual bool put(const void* data, std::size_t size)
	{
		if (size > INT_MAX)
		{
			return false; // TODO
		}

		return send(m_socket, static_cast<const char*>(data), static_cast<int>(size), MSG_NOSIGNAL) == size;
	}

	// send data
	template<typename T> bool put(const T& data)
	{
		return put(&data, sizeof(T));
	}

	// receive data
	virtual bool get(void* data, std::size_t size)
	{
		if (size > INT_MAX)
		{
			return false; // TODO
		}

		return recv(m_socket, static_cast<char*>(data), static_cast<int>(size), MSG_WAITALL) == size;
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
	short_str_t<15> m_received;

public:
	cipher_socket_t(socket_id_t socket, packet_t key)
		: socket_t(socket)
		, m_cipher(std::move(key))
		, m_received({})
	{
	}

	virtual ~cipher_socket_t()
	{
		m_received = {}; // burn
	}

	virtual bool put(const void* data, std::size_t size) override
	{
		const auto asize = size + 15 & ~15;

		std::unique_ptr<rc6_block_t[]> buf(new rc6_block_t[asize / 16]);

		std::memcpy(buf.get(), data, size);

		std::memset(reinterpret_cast<u8*>(buf.get()) + size, 0, asize - size); // zero padding

		for (std::size_t i = 0; i < asize / 16; i++)
		{
			m_cipher.encrypt_block_cbc(buf.get()[i]);
		}

		return socket_t::put(buf.get(), asize);
	}

	virtual bool get(void* data, std::size_t size) override
	{
		// try to get saved data
		if (const auto read = std::min<std::size_t>(m_received.length, size))
		{
			std::memcpy(data, m_received.data, read);
			m_received = short_str_t<15>::make(m_received.data + read, m_received.length - read); // shrink saved data

			size -= read;
			data = static_cast<u8*>(data) + read;
		}

		// try to receive new data from socket
		if (const auto asize = size + 15 & ~15)
		{
			std::unique_ptr<rc6_block_t[]> buf(new rc6_block_t[asize / 16]);

			if (!socket_t::get(buf.get(), asize))
			{
				return false;
			}

			for (u32 i = 0; i < asize / 16; i++)
			{
				m_cipher.decrypt_block_cbc(buf.get()[i]);
			}
			
			std::memcpy(data, buf.get(), size);
			m_received = short_str_t<15>::make(reinterpret_cast<u8*>(buf.get()) + size, asize - size); // save exceeded data

			std::memset(buf.get(), 0, asize); // burn
		}

		return true;
	}

	virtual void flush() override
	{
		m_received = {}; // clear saved data

		return socket_t::flush();
	}
};

class web_socket_t : public socket_t
{
protected:

public:
	virtual bool put(const void* data, std::size_t size) override
	{
		return socket_t::put(data, size);
	}

	virtual bool get(void* data, std::size_t size) override
	{
		return socket_t::get(data, size);
	}

	virtual void flush() override
	{
		return socket_t::flush();
	}
};
