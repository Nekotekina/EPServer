#pragma once
#include <emmintrin.h>
#include "ep_defines.h"

union rc6_block_t
{
	u32 i[4];
	__m128i vi;
};

class rc6_cipher_t final
{
	enum : u32
	{
		rounds = 20,
		minlen = 16,
		keylen = 2 * rounds + 4,

		p32 = 0xb7e15163,
		q32 = 0x9e3779b9,
		lgw = 5,
	};

	std::array<u32, keylen> m_s; // key data

	rc6_block_t m_enc_last;
	rc6_block_t m_dec_last;

public:
	rc6_cipher_t(packet_data_t key);
	~rc6_cipher_t();

	void encrypt_block_cbc(rc6_block_t& block);
	void decrypt_block_cbc(rc6_block_t& block);
};
