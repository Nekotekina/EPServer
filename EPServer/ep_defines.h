#pragma once

// MD5 hash container
using md5_t = std::array<u8, 16>;

// Data packet data
class packet_data_t
{
	std::unique_ptr<u8[]> m_data;
	u32 m_size = 0;

public:
	packet_data_t()
	{
	}

	packet_data_t(u32 size)
		: m_data(new u8[size])
		, m_size(size)
	{
	}

	packet_data_t(packet_data_t&& right)
		: m_data(std::move(right.m_data))
		, m_size(right.m_size)
	{
		right.m_size = 0;
	}

	packet_data_t& operator =(packet_data_t&& right)
	{
		if (this != &right)
		{
			m_data = std::move(right.m_data);
			m_size = right.m_size;
			right.m_size = 0;
		}
		
		return *this;
	}

	~packet_data_t()
	{
		memset(m_data.get(), 0, m_size); // burn
	}

	u8* get() const
	{
		return m_data.get();
	}

	u32 size() const
	{
		return m_size;
	}
};

// Data packet
using packet_t = std::shared_ptr<packet_data_t>;

// Server identifier (UTF8 string)
static const auto vers = "EPClient v0.16";

// Pascal string type (used in some structs)
template<u8 N> struct str_t
{
	static_assert(N, "Invalid str_t size");

	static const u8 size = N;

	u8 length;
	char data[size];

	static str_t make(const char* str)
	{
		str_t res;
		memcpy(res.data, str, res.length = static_cast<u8>(std::min<size_t>(strlen(str), N)));
		return res;
	}

	static str_t make(const char* str, u8 len)
	{
		str_t res;
		memcpy(res.data, str, res.length = len);
		return res;
	}

	template<u8 N2> bool operator == (str_t<N2>& right) const
	{
		return length == right.length && memcmp(data, right.data, length) == 0;
	}

	size_t save(std::FILE* f)
	{
		size_t res = 0;
		res += std::fwrite(&length, sizeof(length), 1, f);
		res += std::fwrite(data, 1, length, f);
		return res;
	}

	size_t load(std::FILE* f)
	{
		*this = {};
		size_t res = 0;
		res += std::fread(&length, sizeof(length), 1, f);
		res += std::fread(data, 1, length, f);
		return res;
	}
};

enum
{
	MAX_OFFTIME = 1000 * 60 * 60, // 60 min
	MAX_WAIT_TIME = 1000 * 60 * 2, // 2 min
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

struct ClientAuthRec
{
	u8 code;
	u16 size;
	str_t<16> name;
	md5_t pass; // md5(md5(password))
};

struct SecureAuthRec
{
	u8 code;
	u16 size;
	str_t<16> name;
	md5_t pass; // md5(md5(password))
	u8 ckey[32]; // session key
};

struct ServerTextRec
{
	static const u16 max_data_size = 65527;

	u8 code;
	u16 size;
	f64 stamp; // message timestamp (OLE automation time)
	char data[max_data_size]; // utf-8 text
};

struct ClientCmdRec
{
	static const u16 max_data_size = 65521;

	u16 cmd;
	s32 v0;
	s32 v1;
	s32 v2;
	char data[max_data_size];
};

struct ServerVersionRec
{
	u8 code;
	u16 size;
	str_t<30> data;
};

struct PlayerElement
{
	str_t<48> name;
	u64 flags;
	s32 gindex;
};

static const size_t MAX_PLAYERS = 65527 / sizeof(PlayerElement);

struct ServerListRec
{
	u8 code;
	u16 size;
	s32 self;
	s32 count;
	PlayerElement data[MAX_PLAYERS];
};

struct ServerUpdatePlayer
{
	u8 code;
	u16 size;
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
	CMD_CHANGE = 7,
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
	SCMD_REFRESH = 3, // obsolete command (refresh player list)
	SCMD_TIMEOUT_QUIT = 4, // obsolete command (does nothing)
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
};

static const char* FlagName[] =
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

static std::string FormatDice(s32 data)
{
	struct DiceData
	{
		u8 count; // dice count
		u8 size; // dice size
		s16 add; // dice modifier

	}& dice = reinterpret_cast<DiceData&>(data);

	s32 res = dice.add;
	for (u32 i = 0; i < dice.count; i++)
	{
		res += rand() % dice.size + 1;
	}

	return std::to_string((s32)dice.count) + "d" + std::to_string((s32)dice.size) + " = " + std::to_string(res);
}

static bool IsLoginValid(const char* str, size_t len)
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
	std::time_t now = std::time(0);

	auto tm = std::gmtime(&now); // get UTC time

	auto days = [](int y) -> int // get number of days since 01.01.0001
	{
		y--;
		return 365 * y + y / 4 - y / 100 + y / 400 + 1;
	};

	return 2.0 + (days(tm->tm_year + 1900) - days(1900)) + tm->tm_yday + tm->tm_hour / 24.0 + tm->tm_min / 1440.0 + tm->tm_sec / 86400.0;
}
