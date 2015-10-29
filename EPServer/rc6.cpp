#include "stdafx.h"
#include "rc6.h"

rc6_cipher_t::rc6_cipher_t(packet_t key)
{
	if (key->size != 16 && key->size != 32 && key->size != 64)
	{
		throw std::length_error("Invalid cipher key size");
	}

	std::memcpy(&m_enc_last, key->data(), 16);
	std::memcpy(&m_dec_last, key->data(), 16);

	u32 L[minlen];

	for (u32 i = 0; i < minlen; i++)
	{
		L[i] = key->get<u32>(i * 4 % key->size);
	}

	m_s[0] = p32;

	for (u32 i = 1; i < keylen; i++)
	{
		m_s[i] = m_s[i - 1] + q32;
	}

	for (u32 i = 0, j = 0, k = 0, A = 0, B = 0; k < 3 * std::max<u32>(minlen, keylen); k++)
	{
		A = m_s[i] = rol32((m_s[i] + A + B), 3);
		B = L[j] = rol32(L[j] + A + B, (A + B));
		i = (i + 1) % keylen;
		j = (j + 1) % minlen;
	}
}

rc6_cipher_t::~rc6_cipher_t()
{
	std::memset(m_s.data(), 0, m_s.size()); // burn
	m_enc_last.clear();
	m_dec_last.clear();
}

void rc6_cipher_t::encrypt_block_cbc(rc6_block_t& block)
{
	// CBC initialization
	block.vi = _mm_xor_si128(block.vi, m_enc_last.vi);

	block.i[1] += m_s[0];
	block.i[3] += m_s[1];

	for (u32 i = 1; i <= rounds; i++)
	{
		const u32 t = rol32(block.i[1] * (2 * block.i[1] + 1), lgw);
		const u32 u = rol32(block.i[3] * (2 * block.i[3] + 1), lgw);
		block.i[0] = rol32(block.i[0] ^ t, u) + m_s[i * 2];
		block.i[2] = rol32(block.i[2] ^ u, t) + m_s[i * 2 + 1];

		block.vi = _mm_alignr_epi8(block.vi, block.vi, 4); // rotate
	}

	block.i[0] += m_s[rounds * 2 + 2];
	block.i[2] += m_s[rounds * 2 + 3];

	m_enc_last = block;
}

void rc6_cipher_t::decrypt_block_cbc(rc6_block_t& block)
{
	const rc6_block_t encrypted = block;

	block.i[0] -= m_s[rounds * 2 + 2];
	block.i[2] -= m_s[rounds * 2 + 3];

	for (u32 i = rounds; i; i--)
	{
		block.vi = _mm_alignr_epi8(block.vi, block.vi, 12); // rotate

		const u32 t = rol32(block.i[1] * (2 * block.i[1] + 1), lgw);
		const u32 u = rol32(block.i[3] * (2 * block.i[3] + 1), lgw);
		block.i[0] = ror32(block.i[0] - m_s[i * 2], u) ^ t;
		block.i[2] = ror32(block.i[2] - m_s[i * 2+ 1], t) ^ u;
	}

	block.i[1] -= m_s[0];
	block.i[3] -= m_s[1];

	// CBC finalization
	block.vi = _mm_xor_si128(block.vi, m_dec_last.vi);

	m_dec_last = encrypted;
}
