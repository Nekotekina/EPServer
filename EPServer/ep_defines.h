#pragma once

// Print logs with current time
template<typename... T> inline void ep_printf(const char* fmt, T&&... args)
{
	print_time();
	std::printf(fmt, std::forward<T>(args)...);
}

inline void ep_printf(const char* fmt)
{
	print_time();
	std::printf("%s", fmt);
}

// MD5 hash container
using md5_t = std::array<unsigned char, 16>;

static_assert(sizeof(md5_t) == 16, "Invalid md5_t size");

class packet_t;

// Data packet data (contains ref counter, data size and possibly data)
class packet_storage_t final
{
	friend class packet_t;

	std::atomic<u32> m_refcnt;
	//std::atomic<u32> m_weakcnt; // currently not used

public:
	const std::size_t size = 0;

	packet_storage_t()
		: m_refcnt(1)
	{
	}

	void* data()
	{
		return this + 1;
	}

	template<typename T = char> T& get(std::size_t offset = 0)
	{
		static_assert(std::is_pod<T>::value, "Invalid get<> type (must be POD)");

		return *reinterpret_cast<T*>(reinterpret_cast<char*>(this + 1) + offset);
	}

	~packet_storage_t()
	{
		std::memset(this + 1, 0, size); // burn
	}
};

static_assert(sizeof(packet_storage_t) == 2 * sizeof(std::size_t), "Invalid packet_storage_t size");

// Data packet (shared dynamic byte array for POD, works much like limited std::shared_ptr)
class packet_t final
{
	packet_storage_t* m_ptr = nullptr;

	void dec_ref()
	{
		if (m_ptr && !--m_ptr->m_refcnt)
		{
			m_ptr->~packet_storage_t();
			std::free(m_ptr);
		}
	}

	void inc_ref()
	{
		if (m_ptr)
		{
			m_ptr->m_refcnt++;
		}
	}

public:
	packet_t()
	{
	}

	packet_t(nullptr_t)
	{
	}

	packet_t(const packet_t& right)
		: m_ptr(right.m_ptr)
	{
		inc_ref();
	}

	packet_t& operator =(const packet_t& right)
	{
		packet_t old;
		old.m_ptr = m_ptr;

		m_ptr = right.m_ptr;
		inc_ref();

		return *this;
	}

	packet_t(packet_t&& right)
		: m_ptr(right.m_ptr)
	{
		right.m_ptr = nullptr;
	}

	packet_t& operator =(packet_t&& right)
	{
		if (this != &right)
		{
			dec_ref();

			m_ptr = right.m_ptr;
			right.m_ptr = nullptr;
		}

		return *this;
	}

	~packet_t()
	{
		dec_ref();
	}

	void reset()
	{
		dec_ref();
		m_ptr = nullptr;
	}

	packet_storage_t* operator *() const
	{
		return m_ptr;
	}

	packet_storage_t* operator ->() const
	{
		return m_ptr;
	}

	operator bool() const
	{
		return m_ptr != nullptr;
	}
};

// Data packet generator
inline packet_t make_packet(std::size_t size)
{
	// allocate memory for ref counter, size and data
	void* pointer = std::malloc(sizeof(packet_storage_t) + size);

	// initialize ref counter
	auto storage = new (pointer)packet_storage_t();

	// rewrite size
	const_cast<std::size_t&>(storage->size) = size;

	return reinterpret_cast<packet_t&&>(pointer);
}

// Server identifier (UTF8 string)
static const auto ep_version = "EPClient v0.16";

// Pascal short string type (used in some structs)
template<u8 N = 255> struct short_str_t
{
	static_assert(N, "Invalid short_str_t size");

	u8 length;
	char data[N];

	static short_str_t make(const void* ptr, std::size_t len)
	{
		short_str_t res;
		std::memcpy(res.data, ptr, res.length = static_cast<u8>(std::min<std::size_t>(len, N)));
		std::memset(res.data + res.length, 0, N - res.length);
		return res;
	}

	operator std::string() const
	{
		return{ data, length };
	}

	template<u8 N2> operator short_str_t<N2>() const
	{
		return short_str_t<N2>::make(data, length);
	}

	template<u8 N2> bool operator ==(const short_str_t<N2>& right) const
	{
		return length == right.length && std::memcmp(data, right.data, length) == 0;
	}

	std::unique_ptr<char[]> c_str() const
	{
		std::unique_ptr<char[]> res(new char[length + 1]);

		std::memcpy(res.get(), data, length);
		res.get()[length] = 0;

		return res;
	}

	std::size_t save(std::FILE* f) const
	{
		std::size_t res = 0;
		res += std::fwrite(&length, 1, 1, f);
		res += std::fwrite(data, 1, length, f);
		return res;
	}

	std::size_t load(std::FILE* f)
	{
		*this = {};
		std::size_t res = 0;
		res += std::fread(&length, 1, 1, f);
		res += std::fread(data, 1, length, f);
		return res;
	}
};
			 
enum ProtocolCmdType : u8
{
	SERVER_AUTH = 255, // server greeting

	CLIENT_AUTH = 0, // login/pass
	SERVER_TEXT = 1, // server message
	CLIENT_CMD = 2, // client command with text and few int params
	CLIENT_SCMD = 3, // client command (only 16 bits)
	SERVER_PLIST = 4, // full player list
	SERVER_DISCONNECT = 5, // disconnect notification
	SERVER_VERSIONINFO = 6, // version "number" notification

	GAME_AUTH = 7,
	SERVER_GLIST = 8,
	GAME_SETTINGS = 9,
	SERVER_GINFO = 10,
	GAME_SELECT = 11,
	GAME_COMMAND = 12,
	SERVER_GAME = 13,

	SERVER_PUPDATE = 14, // update single element in player list
	SERVER_GUPDATE = 15,

	CLIENT_CHANGE_MAP_INFO = 16,
	CLIENT_CHANGE_MAP_HASH = 17,
	CLIENT_UPLOADING = 18,

	CLIENT_SECURE_AUTH = 19,
	SERVER_NONFATALDISCONNECT = 20,
};

#pragma pack(push, 1)

struct ProtocolHeader
{
	u8 code;
	u16 size;
};

struct ClientAuthRec // doesn't include ProtocolHeader
{
	short_str_t<16> name;
	md5_t pass; // md5(md5(password))
};

struct SecureAuthRec // doesn't include ProtocolHeader
{
	ClientAuthRec info;
	char ckey[32]; // session key
};

struct ServerTextRec
{
	enum { max_size = 65527 };

	ProtocolHeader header;
	f64 stamp; // message timestamp (OLE automation time)
	char data[max_size]; // utf-8 text

	static packet_t generate(f64 stamp, const char* text, std::size_t size)
	{
		const u16 tsize = static_cast<u16>(std::min<std::size_t>(size, max_size)) + 8; // data size

		packet_t packet = make_packet(tsize + 3);

		auto& rec = packet->get<ServerTextRec>();
		rec.header = { SERVER_TEXT, tsize };
		rec.stamp = stamp;
		std::memcpy(rec.data, text, tsize - 8);

		return packet;
	}

	static packet_t generate(f64 stamp, const std::string& text)
	{
		return generate(stamp, text.c_str(), text.size());
	}
};

struct ClientCmdRec // doesn't include ProtocolHeader
{
	enum { max_size = 65521 };

	u16 cmd;
	s32 v0;
	s32 v1;
	s32 v2;
	char data[max_size];
};

struct ClientSCmdRec
{
	ProtocolHeader header;
	u16 cmd;
};

struct ServerVersionRec
{
	ProtocolHeader header;
	short_str_t<30> data;
};

struct PlayerElement
{
	short_str_t<48> name;
	u64 flags;
	s32 gindex;
};

static const std::size_t MAX_PLAYERS = 65527 / sizeof(PlayerElement);

struct ServerListRec
{
	ProtocolHeader header;
	s32 self;
	s32 count;
	PlayerElement data[MAX_PLAYERS];
};

struct ServerUpdatePlayer
{
	ProtocolHeader header;
	s32 index;
	PlayerElement data;
};

#pragma pack(pop)

enum ClientCmdType : u16
{
	CMD_NONE = 0, // nothing
	CMD_CHAT = 1, // chat message
	CMD_SET_EMAIL = 2, // change email
	CMD_SET_PASSWORD = 3, // change password
	CMD_SET_FLAG = 4, // change player flag
	CMD_DISCONNECT = 5, // disconnection request
	CMD_INFO = 6, // get player information
	CMD_CHANGE = 7, // (obsolete)
	CMD_SET_NAME = 8, // change unique name
	CMD_CALL = 9, // load account
	CMD_SET_NOTE = 10, // set server greeting
	CMD_SHOUT = 11, // chat message that ignores PF_OFF flag
	CMD_ADD_BAN = 12, // ban specified IP address

	CMD_CREATE_GAME = 13,
	CMD_DELETE_GAME = 14,
	CMD_GAME_OWNER = 15,
	CMD_ADD_PLAYER = 16,
	CMD_DELETE_PLAYER = 17,
	CMD_JOIN_GAME = 18,
	CMD_UPLOAD_MAP = 19,

	CMD_DICE = 20,
};

enum ClientSpecialCmdType : u16 // SCMD commands
{
	SCMD_QUIT = 0, // quit
	SCMD_HIDE = 1, // set PF_OFF flag
	SCMD_SHOW = 2, // remove PF_OFF flag
	SCMD_REFRESH = 3, // refresh player list
	SCMD_TIMEOUT_QUIT = 4, // (obsolete)
	SCMD_NONE = 5, // nothing (used as keepalive)
	SCMD_UPDATE_SERVER = 6,
	SCMD_CONFIRMATION = 7,
};

enum PlayerFlags : u64
{
	PF_GAMEADMIN = 1ull << 0, // game moderator rights
	PF_OFF = 1ull << 1, // offline flag
	PF_LOCK = 1ull << 2, // locked player
	PF_SUPERADMIN = 1ull << 3, // full rights
	PF_NOCHAT = 1ull << 4,
	PF_NOALLYCHAT = 1ull << 5,
	PF_NOPRIVCHAT = 1ull << 6,
	PF_NOGAME = 1ull << 7,
	PF_NOCONNECT = 1ull << 8,
	PF_LOST = 1ull << 9, // connection lost
	PF_NEW_PLAYER = 1ull << 10,

	PF_VOLATILE_FLAGS = PF_LOST | PF_NEW_PLAYER,
};

static const char* const FlagName[] =
{
	"moderator",
	"offline",
	"hold",
	"administrator",
	"no_publicchat",
	"no_allychat",
	"no_privatechat",
	"no_game",
	"ban", "???", "???", "???", "???", "???", "???", "???",
	"???", "???", "???", "???", "???", "???", "???", "???",
	"???", "???", "???", "???", "???", "???", "???", "???",
	"???", "???", "???", "???", "???", "???", "???", "???",
	"???", "???", "???", "???", "???", "???", "???", "???",
	"???", "???", "???", "???", "???", "???", "???", "???",
	"???", "???", "???", "???", "???", "???", "???", "???",
};

static std::string FormatDice(const s32 data)
{
	struct DiceData
	{
		u8 count; // dice count
		u8 size; // dice size
		s16 add; // dice modifier
	}
	const dice = reinterpret_cast<const DiceData&>(data);

	s32 res = dice.add;

	for (u32 i = 0; i < dice.count; i++)
	{
		res += rand() % dice.size + 1;
	}

	auto format_add = [](int add) -> std::string
	{
		if (add < 0) return std::to_string(add);
		if (add > 0) return "+" + std::to_string(add);
		return{};
	};

	return std::to_string(dice.count) + "d" + std::to_string(dice.size) + format_add(dice.add) + " = " + std::to_string(res);
}

static bool IsLoginValid(const char* str, std::size_t len)
{
	if (!len) return false;

	for (; len > 0; str++, len--)
	{
		switch (*str)
		{
		case '_':
		case '+':
		case '-':
		case '=':
		case '.':
		case '(':
		case ')':
			continue;
		default:
			if (!isalnum(*str)) return false;
		}
	}

	return true;
}

// Get current time in days after midnight, 30 December 1899 (OLE automation time)
static f64 GetTime()
{
	const std::time_t now = std::time(0);

	auto tm = std::gmtime(&now); // get UTC time

	auto days = [](int y) -> int // get number of days since 01.01.0001
	{
		y--;
		return 365 * y + y / 4 - y / 100 + y / 400 + 1;
	};

	return 2.0 + (days(tm->tm_year + 1900) - days(1900)) + tm->tm_yday + tm->tm_hour / 24.0 + tm->tm_min / 1440.0 + tm->tm_sec / 86400.0;
}
