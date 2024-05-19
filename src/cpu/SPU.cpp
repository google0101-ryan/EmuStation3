#include "SPU.h"
#include <stdio.h>
#include <stdexcept>
#include <mmintrin.h>
#include <emmintrin.h>
#include <bit>
#include <cstring>
#include <fstream>

#define printf(x, ...) \
	if (id == 0) { printf(x, ##__VA_ARGS__); }

static int g_id = 0;

SPU::SPU(MemoryManager* manager)
: manager(manager)
{
	id = g_id++;
}

void SPU::Run()
{
	for (int i = 0; i < 4; i++)
	{
		if (!running)
			return;

		uint32_t instr = Read32(pc);
		pc += 4;

		if (instr == 0x00400000)
		{
			printf("sync\n");
			return;
		}
		
		if ((instr & 0xFFE00000) == 0x00000000)
		{
			running = false;
			if (thread)
			{
				thread->running = false;
				thread = nullptr;
			}
			printf("stop\n");
			return;
		}
		
		switch ((instr >> 23) & 0x1FF)
		{
		case 0x08:
			ori(instr);
			return;
		case 0x40:
			brz(instr);
			return;
		case 0x42:
			brnz(instr);
			return;
		case 0x47:
			stqr(instr);
			return;
		case 0x64:
			br(instr);
			return;
		case 0x65:
			fsmbi(instr);
			return;
		case 0x66:
			brsl(instr);
			return;
		case 0x67:
			lqr(instr);
			return;
		case 0x81:
			il(instr);
			return;
		case 0x82:
			ilhu(instr);
			return;
		case 0x83:
			ilh(instr);
			return;
		case 0xC1:
			iohl(instr);
			return;
		}
		
		switch ((instr >> 25) & 0x7F)
		{
		case 0x09:
			printf("hbrr\n");
			return;
		case 0x21:
			ila(instr);
			return;
		}

		switch ((instr >> 24) & 0xFF)
		{
		case 0x15:
			andhi(instr);
			return;
		case 0x16:
			andbi(instr);
			return;
		case 0x1c:
			ai(instr);
			return;
		case 0x24:
			stqd(instr);
			return;
		case 0x34:
			lqd(instr);
			return;
		case 0x4c:
			cgti(instr);
			return;
		case 0x5e:
			clgtbi(instr);
			return;
		case 0x7c:
			ceqi(instr);
			return;
		}

		switch ((instr >> 21) & 0x7FF)
		{
		case 0x01:
			printf("lnop\n");
			return;
		case 0x0D:
			rdch(instr);
			return;
		case 0x40:
			sf(instr);
			return;
		case 0x48:
			sfh(instr);
			return;
		case 0x79:
			rotmi(instr);
			return;
		case 0x7A:
			rotmai(instr);
			return;
		case 0x7b:
			shli(instr);
			return;
		case 0xC0:
			a(instr);
			return;
		case 0x10d:
			wrch(instr);
			return;
		case 0x144:
			stqx(instr);
			return;
		case 0x1a8:
			bi(instr);
			return;
		case 0x1ac:
			hbr(instr);
			return;
		case 0x1c4:
			lqx(instr);
			return;
		case 0x1dc:
			rotqby(instr);
			return;
		case 0x1f4:
			cbd(instr);
			return;
		case 0x1f6:
			cwd(instr);
			return;
		case 0x1fc:
			rotqbyi(instr);
			return;
		case 0x1fd:
			rotqmbyi(instr);
			return;
		case 0x1ff:
			shlqbyi(instr);
			return;
		case 0x201:
			printf("nop\n");
			return;
		}

		switch ((instr >> 28) & 0xF)
		{
		case 0x8:
			selb(instr);
			return;
		case 0xb:
			shufb(instr);
			return;
		default:
			printf("Unknown instruction 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x (0x%08x)\n", (instr >> 28) & 0xF, (instr >> 21) & 0x7FF, (instr >> 24) & 0xFF, (instr >> 25) & 0x7F, (instr >> 23) & 0x1FF, instr);
			exit(1);
		}
	}
}

void SPU::Dump()
{
	std::ofstream lsOut("spu" + std::to_string(id) + "_lsa.bin");

	lsOut.write((char*)localStore, 256*1024);
	lsOut.close();

	for (int i = 0; i < 128; i++)
		printf("$%d\t->\t%016" PRIX64 "%016" PRIX64 "\n", i, gprs[i].u64[1], gprs[i].u64[0]);
	printf("pc\t->\t0x%08x\n", pc);
}

void SPU::SetThread(SpuThread *thread)
{
	this->thread = thread;

	pc = thread->entry;

	gprs[3].u64[1] = thread->arg0;
	gprs[4].u64[1] = thread->arg1;
	gprs[5].u64[1] = thread->arg2;
	gprs[6].u64[1] = thread->arg3;

	printf("0x%08lx, 0x%08lx, 0x%08lx, 0x%08lx\n", thread->arg0, thread->arg1, thread->arg2, thread->arg3);

	running = true;
	thread->running = true;
}

void SPU::SetEntry(uint32_t entry)
{
	pc = entry;
}

void SPU::WriteProblemStorage(uint32_t reg, uint32_t data)
{
	switch (reg)
	{
	case 0x401C:
		running = data & 1;
		break;
	case 0x1400C:
	{
		if (running == false && waitingForSignal1 == true)
		{
			running = true;
			waitingForSignal1 = false;
		}
		signal1.push(data);
		break;
	}
	default:
		printf("Write to unknown problem storage register 0x%04x\n", reg);
		throw std::runtime_error("Unknown problem storage register");
	}
}

uint32_t SPU::ReadProblemStorage(uint32_t reg)
{
	switch (reg)
	{
	case 0x4004:
		return spu_out_mbox;
	case 0x4014:
		return mbox_has_data;
	default:
		printf("Read from unknown problem storage register 0x%04x\n", reg);
		throw std::runtime_error("Unknown problem storage register");
	}
}

void SPU::ori(uint32_t instr)
{
	uint8_t rt = instr & 0x7F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint32_t i10 = (instr >> 14) & 0x3FF;

	i10 = (i10 & 0x200) ? (i10 | 0xFFFFFE00) : i10;

	if (i10 == 0)
	{
		memcpy(&gprs[rt], &gprs[ra], sizeof(SpuReg));
		return;
	}

	for (int i = 0; i < 4; i++)
		gprs[rt].u32[i] = gprs[ra].u32[i] | i10;
	
	printf("ori $%d, $%d, 0x%08x\n", rt, ra, i10);
}

void SPU::brz(uint32_t instr)
{
	uint8_t rt = instr & 0x7F;
	int32_t i16 = ((int16_t)((instr >> 7) & 0xFFFF)) << 2;

	printf("brz $%d,0x%08x\n", rt, pc+i16-4);

	if (gprs[rt].u32[3] == 0)
		pc += i16-4;
}

void SPU::brnz(uint32_t instr)
{
	uint8_t rt = instr & 0x7F;
	int32_t i16 = ((int16_t)((instr >> 7) & 0xFFFF)) << 2;

	printf("brnz $%d,0x%08x\n", rt, pc+i16-4);

	if (gprs[rt].u32[3] != 0)
		pc += i16-4;
}

void SPU::stqr(uint32_t instr)
{
	uint32_t i16 = (instr >> 7) & 0xFFFF;
	i16 <<= 2;

	uint32_t ea = i16 + (pc-4);
	uint8_t rt = instr & 0x7F;

	printf("stqr $%d, 0x%08x\n", rt, ea);

	Write128(ea, gprs[rt]);
}

void SPU::br(uint32_t instr)
{
	int32_t i16 = ((int16_t)((instr >> 7) & 0xFFFF)) << 2;

	pc += i16-4;

	printf("br 0x%08x\n", pc);
}

void SPU::fsmbi(uint32_t instr)
{
	uint8_t rt = instr & 0x7F;
	int32_t i16 = (int16_t)((instr >> 7) & 0xFFFF);

	for (int i = 0; i < 16; i++)
	{
		if ((i16 >> (15-i) & 1) == 0)
			gprs[rt].u8[i] = 0;
		else
			gprs[rt].u8[i] = 0xFF;
	}

	printf("fsmbi $%d, 0x%04x\n", rt, i16);
}

void SPU::brsl(uint32_t instr)
{
	uint8_t rt = instr & 0x7F;
	int32_t i16 = ((int16_t)((instr >> 7) & 0xFFFF)) << 2;

	gprs[rt].u32[3] = pc;
	pc += i16-4;

	printf("brsl $%d,0x%08x\n", rt, pc);
}

void SPU::il(uint32_t instr)
{
	uint8_t rt = instr & 0x7F;
	int32_t i16 = (int16_t)((instr >> 7) & 0xFFFF);

	for (int i = 0; i < 4; i++)
		gprs[rt].u32[i] = (uint32_t)i16;
	
	printf("il $%d, 0x%08x\n", rt, (uint32_t)i16);
}

void SPU::ilhu(uint32_t instr)
{
	uint8_t rt = instr & 0x7F;
	int32_t i16 = (int16_t)((instr >> 7) & 0xFFFF);

	for (int i = 0; i < 4; i++)
		gprs[rt].u32[i] = (uint32_t)(i16 << 16);
	
	printf("ilhu $%d, 0x%08x\n", rt, (uint32_t)i16);
}

void SPU::ilh(uint32_t instr)
{
	uint8_t rt = instr & 0x7F;
	int32_t i16 = (int16_t)((instr >> 7) & 0xFFFF);

	for (int i = 0; i < 8; i++)
		gprs[rt].u16[i] = i16;
	
	printf("ilh $%d, 0x%04x\n", rt, i16);
}

void SPU::iohl(uint32_t instr)
{
	uint8_t rt = instr & 0x7F;
	uint32_t i16 = ((instr >> 7) & 0xFFFF);

	for (int i = 0; i < 4; i++)
		gprs[rt].u32[i] |= i16;
	
	printf("iohl $%d, 0x%08x\n", rt, (uint32_t)i16);
}

void SPU::ila(uint32_t instr)
{
	uint8_t rt = instr & 0x7F;
	uint32_t i18 = (instr >> 7) & 0x3FFFF;

	for (int i = 0; i < 4; i++)
		gprs[rt].u32[i] = i18;
	
	printf("ila $%d, 0x%08x\n", rt, i18);
}

void SPU::andhi(uint32_t instr)
{
	uint32_t i10 = (instr >> 14) & 0x3FF;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	i10 = (i10 & 0x200) ? (i10 | 0xFE00) : i10;

	for (int i = 0; i < 8; i++)
	{
		gprs[rt].u16[i] = gprs[ra].u16[i] & i10;
	}

	printf("andhi $%d,$%d,%d\n", rt, ra, (int16_t)i10);
}

void SPU::andbi(uint32_t instr)
{
	uint8_t i10 = (instr >> 14) & 0xFF;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	for (int i = 0; i < 16; i++)
	{
		gprs[rt].u8[i] = gprs[ra].u8[i] & i10;
	}

	printf("andbi $%d,$%d,%d\n", rt, ra, i10);
}

void SPU::ai(uint32_t instr)
{
	int32_t i10 = (instr >> 14) & 0x3FF;
	i10 = (i10 & 0x200) ? (i10 | 0xFFFFFE00) : i10;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	for (int i = 0; i < 4; i++)
		gprs[rt].u32[i] = gprs[ra].u32[i] + i10;
	
	printf("ai $%d, $%d, %d\n", rt, ra, i10);
}

void SPU::stqd(uint32_t instr)
{
	uint32_t imm = (instr >> 14) & 0x3FF;
	imm <<= 4;
	imm = (imm & 0x2000) ? (imm | 0xFFFFE000) : imm;
	int32_t i10 = (int32_t)imm;

	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	uint32_t ea = gprs[ra].u32[3] + i10;
	Write128(ea, gprs[rt]);

	printf("stqd $%d, %d($%d)\n", rt, i10, ra);
}

void SPU::lqd(uint32_t instr)
{
	uint32_t imm = (instr >> 14) & 0x3FF;
	imm <<= 4;
	imm = (imm & 0x2000) ? (imm | 0xFFFFE000) : imm;
	int32_t i10 = (int32_t)imm;

	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	uint32_t ea = gprs[ra].u32[3] + i10;
	gprs[rt] = Read128(ea);

	printf("lqd $%d, %d($%d)\n", rt, i10, ra);
}

void SPU::lqr(uint32_t instr)
{
	uint8_t rt = instr & 0x7F;
	int32_t i16 = ((int16_t)((instr >> 7) & 0xFFFF)) << 2;

	uint32_t ea = pc+i16-4;

	gprs[rt] = Read128(ea);

	printf("lqr $%d, 0x%08x\n", rt, ea);
}

void SPU::cgti(uint32_t instr)
{
	int32_t i10 = (instr >> 14) & 0x3FF;
	i10 = (i10 & 0x200) ? (i10 | 0xFFFFFE00) : i10;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	for (int i = 0; i < 4; i++)
	{
		gprs[rt].u32[i] = (gprs[ra].u32[i] > i10) ? 0xFFFFFFFF : 0x00000000;
	}

	printf("cgti $%d, $%d, 0x%08x\n", rt, ra, i10);
}

void SPU::clgtbi(uint32_t instr)
{
	uint8_t i10 = (instr >> 14) & 0xFF;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	for (int i = 0; i < 16; i++)
	{
		gprs[rt].u8[i] = (gprs[ra].u8[i] > i10) ? 0xFF : 0x00;
	}

	printf("clgtbi $%d,$%d,%d\n", rt, ra, i10);
}

void SPU::ceqi(uint32_t instr)
{
	int32_t i10 = (instr >> 14) & 0x3FF;
	i10 = (i10 & 0x200) ? (i10 | 0xFFFFFE00) : i10;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	for (int i = 0; i < 4; i++)
	{
		gprs[rt].u32[i] = (gprs[ra].u32[i] == i10) ? 0xFFFFFFFF : 0x00000000;
	}

	printf("ceqi $%d, $%d, 0x%08x\n", rt, ra, i10);
}

void SPU::rdch(uint32_t instr)
{
	uint8_t ca = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;
	
	printf("rdch %d, $%d\n", ca, rt);

	gprs[rt].u128 = 0;

	switch (ca)
	{
	case 3:
		if (signal1.empty())
		{
			running = false;
			waitingForSignal1 = true;
			pc -= 4;
		}
		else
		{
			gprs[rt].u32[3] = signal1.front();
			signal1.pop();
		}
		break;
	case 24:
		gprs[rt].u32[3] = mfc_tag_mask; // Just tell the SPU that all operations have been completed
		break;
	default:
		printf("Unknown channel\n");
		exit(1);
	}
}

void SPU::sf(uint32_t instr)
{
	uint8_t rb = (instr >> 14) & 0x7F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	for (int i = 0; i < 4; i++)
		gprs[rt].u32[i] = gprs[rb].u32[i] - gprs[ra].u32[i];
	
	printf("sf $%d, $%d, $%d\n", rt, ra, rb);
}

void SPU::sfh(uint32_t instr)
{
	uint8_t rb = (instr >> 14) & 0x7F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	for (int i = 0; i < 8; i++)
		gprs[rt].u16[i] = gprs[rb].u16[i] - gprs[ra].u16[i];
	
	printf("sfh $%d, $%d, $%d\n", rt, ra, rb);
}

void SPU::a(uint32_t instr)
{
	uint8_t rb = (instr >> 14) & 0x7F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	for (int i = 0; i < 4; i++)
		gprs[rt].u32[i] = (int32_t)gprs[ra].u32[i] + (int32_t)gprs[rb].u32[i];
	
	printf("a $%d, $%d, $%d\n", rt, ra, rb);
}

void SPU::rotmi(uint32_t instr)
{
	uint8_t i7 = (instr >> 14) & 0x7F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	i7 = (i7 & 0x40) ? i7 | 0xC0 : i7;

	uint32_t shiftCount = (0 - i7) & 0x3F;

	if (shiftCount < 32)
	{
		for (int i = 0; i < 4; i++)
			gprs[rt].u32[i] = gprs[ra].u32[ra] >> shiftCount;
	}
	else
		memset(&gprs[rt], 0, sizeof(SpuReg));
	
	printf("rotmi $%d,$%d,%d\n", rt, ra, (int8_t)i7);
}

void SPU::rotmai(uint32_t instr)
{
	uint8_t i7 = (instr >> 14) & 0x7F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	i7 = (i7 & 0x40) ? i7 | 0xC0 : i7;

	uint32_t shiftCount = (0 - i7) & 0x3F;

	if (shiftCount < 32)
	{
		for (int i = 0; i < 4; i++)
			gprs[rt].u32[i] = ((int32_t)gprs[ra].u32[ra]) >> shiftCount;
	}
	else
		memset(&gprs[rt], 0, sizeof(SpuReg));
	
	printf("rotmai $%d,$%d,%d\n", rt, ra, (int8_t)i7);
}

void SPU::shli(uint32_t instr)
{
	uint8_t i7 = (instr >> 14) & 0x3F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	for (int i = 0; i < 4; i++)
	{
		if (i7 < 32)
			gprs[rt].u32[i] = gprs[ra].u32[i] << i7;
		else
			gprs[rt].u32[i] = 0;
	}

	printf("shli $%d,$%d,%d\n", rt, ra, i7);
}

void SPU::wrch(uint32_t instr)
{
	uint8_t ca = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;
	
	printf("wrch %d, $%d\n", ca, rt);

	switch (ca)
	{
	case 16:
		mfc_lsa = gprs[rt].u32[3];
		break;
	case 17:
		printf("0x%08lx -> mfc_eah\n", gprs[rt].u32[3]);
		mfc_ea.hi = gprs[rt].u32[3];
		break;
	case 18:
		printf("0x%08lx -> mfc_eal\n", gprs[rt].u32[3]);
		mfc_ea.lo = gprs[rt].u32[3];
		break;
	case 19:
		printf("0x%08lx -> mfc_len\n", gprs[rt].u32[3]);
		mfc_len = gprs[rt].u32[3];
		break;
	case 20:
		printf("0x%08lx -> mfc_tag\n", gprs[rt].u32[3]);
		break;
	case 21:
	{
		uint8_t cmd = gprs[rt].u32[3];
		if (cmd == 0x40)
		{
			// TODO: Maybe don't make DMA stuff instant?
			for (int i = 0; i < mfc_len; i++)
			{
				localStore[mfc_lsa++] = manager->Read8(mfc_ea.ea++);
			}
			printf("Ran mfc dma command 0x%02x (transferred %d bytes from 0x%08lx -> 0x%08x\n", gprs[rt].u32[3], mfc_len, mfc_ea.ea-mfc_len, mfc_lsa-mfc_len);
		}
		else if (cmd == 0x20)
		{
			// TODO: Maybe don't make DMA stuff instant?
			for (int i = 0; i < mfc_len; i++)
			{
				manager->Write8(mfc_ea.ea++, localStore[mfc_lsa++]);
			}
			printf("Ran mfc dma command 0x%02x (transferred %d bytes from 0x%08lx -> 0x%08x\n", gprs[rt].u32[3], mfc_len, mfc_lsa-mfc_len, mfc_ea.ea-mfc_len);
		}
		else
		{
			printf("Unknown mfc command 0x%02x\n", cmd);
			exit(1);
		}
		break;
	}
	case 22:
		mfc_tag_mask = gprs[rt].u32[3];
		break;
	case 23:
		break;
	case 28:
		spu_out_mbox = gprs[rt].u32[3];
		mbox_has_data = true;
		break;
	default:
		printf("Unknown channel\n");
		exit(1);
	}
}

void SPU::stqx(uint32_t instr)
{
	uint8_t rb = (instr >> 14) & 0x7F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	uint32_t ea = gprs[ra].u32[3] + gprs[rb].u32[3];

	Write128(ea, gprs[rt]);

	printf("stqx $%d,$%d,$%d\n", rt, ra, rb);
}

void SPU::bi(uint32_t instr)
{
	uint8_t ra = (instr >> 7) & 0x7F;
	pc = gprs[ra].u32[3];

	printf("br $%d\n", ra);
}

void SPU::hbr(uint32_t instr)
{
	printf("hbr\n");
}

void SPU::lqx(uint32_t instr)
{
	uint8_t rb = (instr >> 14) & 0x7F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	uint32_t ea = gprs[ra].u32[3] + gprs[rb].u32[3];

	gprs[rt] = Read128(ea);

	printf("lqx $%d,$%d,$%d (0x%08x)\n", rt, ra, rb, ea);
}

void SPU::rotqby(uint32_t instr)
{
	uint8_t rb = (instr >> 14) & 0x7F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;
	
	const auto a = gprs[ra].vi;
	alignas(32) const __m128i buf[2]{a, a};
	gprs[rt].vi = _mm_loadu_si128(reinterpret_cast<const __m128i*>(reinterpret_cast<const uint8_t*>(buf) + (16 - (gprs[rb].u32[3] & 0xf))));
	
	printf("rotqby $%d, $%d, $%d\n", rt, ra, rb);	
}

void SPU::cbd(uint32_t instr)
{
	uint8_t i7 = (instr >> 14) & 0x7F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	auto t = (~(i7 + gprs[ra].u32[3])) & 0xf;

	gprs[rt].u64[0] = 0x18191A1B1C1D1E1FULL;
	gprs[rt].u64[1] = 0x1011121314151617ULL;
	gprs[rt].u8[t] = 0x03;

	printf("cbd $%d,$%d,%d\n", rt, ra, i7);
}

void SPU::cwd(uint32_t instr)
{
	uint8_t i7 = (instr >> 14) & 0x7F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	auto t = (~(i7 + gprs[ra].u32[3]) & 0xC) >> 2;

	gprs[rt].u64[0] = 0x18191A1B1C1D1E1FULL;
	gprs[rt].u64[1] = 0x1011121314151617ULL;
	gprs[rt].u32[t] = 0x00010203;

	printf("cwd $%d, %d($%d)\n", rt, i7, ra);
}

void SPU::rotqbyi(uint32_t instr)
{
	uint8_t i7 = (instr >> 14) & 0x7F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	auto res = gprs[ra];

	for (int i = 0; i < (i7 & 0xF); i++)
	{
		// Left shift: horribly innefficient
		uint8_t u8 = res.u8[15];
		res.u8[15] = res.u8[0];
		res.u8[0] = res.u8[1];
		res.u8[1] = res.u8[2];
		res.u8[2] = res.u8[3];
		res.u8[3] = res.u8[4];
		res.u8[4] = res.u8[5];
		res.u8[5] = res.u8[6];
		res.u8[6] = res.u8[7];
		res.u8[7] = res.u8[8];
		res.u8[8] = res.u8[9];
		res.u8[9] = res.u8[10];
		res.u8[10] = res.u8[11];
		res.u8[11] = res.u8[12];
		res.u8[12] = res.u8[13];
		res.u8[13] = res.u8[14];
		res.u8[14] = u8;
	}

	gprs[rt] = res;

	printf("rotqbyi $%d, $%d, %d\n", rt, ra, i7);
}

void SPU::rotqmbyi(uint32_t instr)
{
	int8_t i7 = (instr >> 14) & 0x7F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	i7 = (i7 & 0x40) ? (i7 | 0xC0) : i7;

	int shiftCount = (0 - i7) & 0x1f;

	for (int i = 0; i < 16; i++)
	{
		if (15 - (i - shiftCount) < 0 || 15 - (i - shiftCount) > 15)
		{
			gprs[rt].u8[i] = 0;
			continue;
		}

		gprs[rt].u8[15 - i] = gprs[ra].u8[15 - (i - shiftCount)];
	}

	printf("rotqmbyi $%d,$%d,%d\n", rt, ra, i7);
}

void SPU::shlqbyi(uint32_t instr)
{
	int8_t i7 = (instr >> 14) & 0x7F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rt = instr & 0x7F;

	for (int i = 0; i < 15; i++)
	{
		if (i7 > 15) gprs[rt].u8[i] = 0;
		else gprs[rt].u8[15 - i] = gprs[ra].u8[15 - (i + i7)];
	}

	printf("shlqbyi $%d,$%d,%d\n", rt, ra, i7);
}

void SPU::selb(uint32_t instr)
{
	uint8_t rt = (instr >> 21) & 0x7F;
	uint8_t rb = (instr >> 14) & 0x7F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rc = instr & 0x7F;

	gprs[rt].u128 = (gprs[rc].u128 & gprs[rb].u128) | ((~gprs[rc].u128) & gprs[ra].u128);

	printf("selb $%d,$%d,$%d,$%d\n", rt, ra, rb, rc);
}

void SPU::shufb(uint32_t instr)
{
	uint8_t rt = (instr >> 21) & 0x7F;
	uint8_t rb = (instr >> 14) & 0x7F;
	uint8_t ra = (instr >> 7) & 0x7F;
	uint8_t rc = instr & 0x7F;

	__m128i ab[2]{gprs[rb].vi, gprs[ra].vi};
	auto c = gprs[rc];
	SpuReg x;
	x.vi = _mm_andnot_si128(c.vi, _mm_set1_epi8(0x1f));
	SpuReg res;

	for (int i = 0; i < 16; i++)
	{
		res.u8[i] = reinterpret_cast<uint8_t*>(ab)[x.u8[i]];
	}

	const auto xc0 = _mm_set1_epi8(static_cast<uint8_t>(0xc0));
	const auto xe0 = _mm_set1_epi8(static_cast<uint8_t>(0xe0));
	const auto cmp0 = _mm_cmpgt_epi8(_mm_setzero_si128(), c.vi);
	const auto cmp1 = _mm_cmpeq_epi8(_mm_and_si128(c.vi, xc0), xc0);
	const auto cmp2 = _mm_cmpeq_epi8(_mm_and_si128(c.vi, xe0), xc0);
	gprs[rt].vi = _mm_or_si128(_mm_andnot_si128(cmp0, res.vi), _mm_avg_epu8(cmp1, cmp2));

	printf("shufb $%d,$%d,$%d,$%d\n", rt, ra, rb, rc);
}
