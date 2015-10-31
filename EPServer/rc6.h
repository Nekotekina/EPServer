#pragma once

#include <emmintrin.h>
#include "ep_defines.h"

union rc6_block_t
{
	u32 i[4];
	__m128i vi;

	// Attempt to fix heap allocation alignment on x86 version (disabled):
	
	//void* operator new[](std::size_t size)
	//{
	//	return _mm_malloc(size, __alignof(rc6_block_t));
	//}

	//void operator delete[](void* p)
	//{
	//	return _mm_free(p);
	//}

	void clear()
	{
		vi = _mm_setzero_si128();
	}
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
	rc6_cipher_t(const packet_t& key);
	~rc6_cipher_t();

	void encrypt_block_cbc(rc6_block_t& block);
	void decrypt_block_cbc(rc6_block_t& block);
};
