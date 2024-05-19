#pragma once

#include <stdint.h>
#include <stdio.h>
#include <queue>
#include <inttypes.h>
#include <emmintrin.h>
#include <kernel/Modules/CellSpurs.h>
#include <kernel/Memory.h>

union SpuReg
{
	unsigned __int128 u128;
	__m128i vi;
	uint64_t u64[2];
	uint32_t u32[4];
	uint16_t u16[8];
	uint8_t u8[16];
};

inline SpuReg andnot(const SpuReg& left, const SpuReg& right)
{
	SpuReg ret;
	ret.vi = _mm_andnot_si128(left.vi, right.vi);
	return ret;
}

inline SpuReg operator&(const SpuReg& left, const SpuReg& right)
{
	SpuReg ret;
	ret.vi = _mm_and_si128(left.vi, right.vi);
	return ret;
}

inline SpuReg operator|(const SpuReg& left, const SpuReg& right)
{
	SpuReg ret;
	ret.vi = _mm_or_si128(left.vi, right.vi);
	return ret;
}

class SPU
{
public:
	SPU(MemoryManager* manager);
	void Run();
	void Dump();

	void SetThread(SpuThread* thread);

	void SetEntry(uint32_t entry);
	void WriteProblemStorage(uint32_t reg, uint32_t data);
	uint32_t ReadProblemStorage(uint32_t reg);

	void Write8(uint32_t offs, uint8_t data)
	{
		localStore[offs] = data;
	}

	void Write128(uint32_t offs, SpuReg data)
	{
		for (int i = 15; i >= 0; i--)
			localStore[offs++] = data.u8[i];
	}

	SpuReg Read128(uint32_t offs)
	{
		SpuReg ret;
		for (int i = 15; i >= 0; i--)
			ret.u8[i] = localStore[offs++];
		return ret;
	}

	uint32_t Read32(uint32_t offs)
	{
		return __builtin_bswap32(*(uint32_t*)&localStore[offs]);
	}
private:
	MemoryManager* manager;
	SpuThread* thread;

	uint8_t localStore[256*1024];
	bool running = false;
	bool waitingForSignal1 = true;

	uint32_t pc;
	int id;

	SpuReg gprs[128];

	// Channels
	uint32_t spu_out_mbox;
	bool mbox_has_data = false;
	// Local store address
	uint32_t mfc_lsa = 0;
	// Main memory address
	union
	{
		struct
		{
			uint64_t lo : 32;
			uint64_t hi : 32;
		};
		uint64_t ea;
	} mfc_ea;
	// mfc dma length
	uint32_t mfc_len;
	// mfc tag mask
	uint32_t mfc_tag_mask;
	// signal1
	std::queue<uint32_t> signal1;

	void ori(uint32_t instr); // 0x08
	void brz(uint32_t instr); // 0x40
	void brnz(uint32_t instr); // 0x42
	void stqr(uint32_t instr); // 0x47
	void br(uint32_t instr); // 0x64
	void fsmbi(uint32_t instr); // 0x65
	void brsl(uint32_t instr); // 0x66
	void il(uint32_t instr); // 0x81
	void ilhu(uint32_t instr); // 0x82
	void ilh(uint32_t instr); // 0x83
	void iohl(uint32_t instr); // 0xc1

	void ila(uint32_t instr); // 0x21

	void andhi(uint32_t instr); // 0x15
	void andbi(uint32_t instr); // 0x16
	void ai(uint32_t instr); // 0x1C
	void stqd(uint32_t instr); // 0x24
	void lqd(uint32_t instr); // 0x34
	void lqr(uint32_t instr); // 0x36
	void cgti(uint32_t instr); // 0x4c
	void clgtbi(uint32_t instr); // 0x5e
	void ceqi(uint32_t instr); // 0x7c

	void rdch(uint32_t instr); // 0x0d
	void sf(uint32_t instr); // 0x40
	void sfh(uint32_t instr); // 0x48
	void a(uint32_t instr); // 0xc0
	void rotmi(uint32_t instr); // 0x79
	void rotmai(uint32_t instr); // 0x7a
	void shli(uint32_t instr); // 0x7b
	void wrch(uint32_t instr); // 0x10d
	void stqx(uint32_t instr); // 0x144
	void bi(uint32_t instr); // 0x1a8
	void hbr(uint32_t instr); // 0x1ac
	void lqx(uint32_t instr); // 0x1c4
	void rotqby(uint32_t instr); // 0x1dc
	void cbd(uint32_t instr); // 0x1f4
	void cwd(uint32_t instr); // 0x1f6
	void rotqbyi(uint32_t instr); // 0x1fc
	void rotqmbyi(uint32_t instr); // 0x1fd
	void shlqbyi(uint32_t instr); // 0x1ff

	void selb(uint32_t instr); // 0x8
	void shufb(uint32_t instr); // 0xb
};

extern SPU* g_spus[6];