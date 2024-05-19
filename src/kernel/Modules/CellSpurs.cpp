#include "CellSpurs.h"
#include "util.h"
#include <cpu/PPU.h>
#include <stdexcept>
#include <cassert>

BEGIN_BE_STRUCT(Elf32Header)
    uint8_t e_ident[16];
    BE_MEMBER_16(e_type);
    BE_MEMBER_16(e_machine);
    BE_MEMBER_32(e_version);
	BE_MEMBER_32(e_entry);
    BE_MEMBER_32(e_phoff);
    BE_MEMBER_32(e_shoff);
    BE_MEMBER_32(e_flags);
    BE_MEMBER_16(e_ehsize);
    BE_MEMBER_16(e_phentsize);
    BE_MEMBER_16(e_phnum);
    BE_MEMBER_16(e_shentsize);
    BE_MEMBER_16(e_shnum);
    BE_MEMBER_16(e_shstrndx);
END_BE_STRUCT();

BEGIN_BE_STRUCT(Elf32Phdr)
    BE_MEMBER_32(p_type);
    BE_MEMBER_32(p_offset);
    BE_MEMBER_32(p_vaddr);
    BE_MEMBER_32(p_paddr);
    BE_MEMBER_32(p_filesz);
    BE_MEMBER_32(p_memsz);
    BE_MEMBER_32(p_flags);
    BE_MEMBER_32(p_align);
END_BE_STRUCT();

static_assert(sizeof(Elf32Phdr) == 0x20);

enum SpursAttrFlags : uint32_t
{
	SAF_NONE                          = 0x00000000,
	SAF_EXIT_IF_NO_WORK               = 0x00000001,
	SAF_UNKNOWN_FLAG_30               = 0x00000002,
	SAF_SECOND_VERSION                = 0x00000004,
	SAF_UNKNOWN_FLAG_9                = 0x00400000,
	SAF_UNKNOWN_FLAG_8                = 0x00800000,
	SAF_UNKNOWN_FLAG_7                = 0x01000000,
	SAF_SYSTEM_WORKLOAD_ENABLED       = 0x02000000,
	SAF_SPU_PRINTF_ENABLED            = 0x10000000,
	SAF_SPU_TGT_EXCLUSIVE_NON_CONTEXT = 0x20000000,
	SAF_SPU_MEMORY_CONTAINER_SET      = 0x40000000,
	SAF_UNKNOWN_FLAG_0                = 0x80000000,
};

uint32_t CellSpurs::cellSpursAttributeInitialize(uint32_t attrPtr, uint32_t revision, 
													uint32_t sdkVersion, uint32_t nSpus, 
													int32_t spuPriority, int32_t ppuPriority, 
													bool exitIfNoWork, CellPPU* ppu)
{
	printf("_cellSpursAttributeInitialize(attr=*0x%x, revision=%d, sdkVersion=0x%x, nSpus=%d, spuPriority=%d, ppuPriority=%d, exitIfNoWork=%d)\n",
		attrPtr, revision, sdkVersion, nSpus, spuPriority, ppuPriority, exitIfNoWork);
	
	if (!attrPtr)
	{
		return 0x80410711;
	}

	if (attrPtr & 3)
	{
		return 0x80410710;
	}

	ppu->GetManager()->Write32(attrPtr+0x00, revision);
	ppu->GetManager()->Write32(attrPtr+0x04, sdkVersion);
	ppu->GetManager()->Write32(attrPtr+0x08, nSpus);
	ppu->GetManager()->Write32(attrPtr+0x0C, spuPriority);
	ppu->GetManager()->Write32(attrPtr+0x10, ppuPriority);
	ppu->GetManager()->Write8(attrPtr+0x14, exitIfNoWork);

	return CELL_OK;
}

uint32_t CellSpurs::cellSpursAttributeEnableSpuPrintfIfAvailable(uint32_t attrPtr, CellPPU* ppu)
{
	printf("cellSpursAttributeEnableSpuPrintfIfAvailable(attr=*0x%08x)\n", attrPtr);
	ppu->GetManager()->Write32(attrPtr+0x28, ppu->GetManager()->Read32(attrPtr+0x28) | SAF_SPU_PRINTF_ENABLED);
	return CELL_OK;
}

class SpuElfLoader
{
public:
	SpuElfLoader(uint8_t* ptr)
	{
		basePtr = ptr;

		for (int i = 0; i < 16; i++)
			ehdr.e_ident[i] = *ptr++;
		
		if (ehdr.e_ident[1] != 'E' || ehdr.e_ident[2] != 'L'
			|| ehdr.e_ident[3] != 'F')
		{
			printf("MALFORMED SPU ELF\n");
			printf("%c%c%c\n", ehdr.e_ident[1], ehdr.e_ident[2], ehdr.e_ident[3]);
			throw std::runtime_error("Invalid SPU ELF");
		}

		ehdr.Read_e_type(ptr);
		ehdr.Read_e_machine(ptr);
		ehdr.Read_e_version(ptr);
		ehdr.Read_e_entry(ptr);
		ehdr.Read_e_phoff(ptr);
		ehdr.Read_e_shoff(ptr);
		ehdr.Read_e_flags(ptr);
		ehdr.Read_e_ehsize(ptr);
		ehdr.Read_e_phentsize(ptr);
		ehdr.Read_e_phnum(ptr);
		ehdr.Read_e_shentsize(ptr);
		ehdr.Read_e_shnum(ptr);
		ehdr.Read_e_shstrndx(ptr);

		printf("Loading SPU ELF with %d program headers, entry 0x%08x\n", ehdr.e_phnum, ehdr.e_entry);

		for (int i = 0; i < ehdr.e_phnum; i++)
		{
			ptr = basePtr + ehdr.e_phoff + (ehdr.e_phentsize * i);

			Elf32Phdr phdr;
			phdr.Read_p_type(ptr);
			phdr.Read_p_offset(ptr);
			phdr.Read_p_vaddr(ptr);
			phdr.Read_p_paddr(ptr);
			phdr.Read_p_filesz(ptr);
			phdr.Read_p_memsz(ptr);
			phdr.Read_p_flags(ptr);
			phdr.Read_p_align(ptr);

			if (phdr.p_type != 1)
				continue;
			
			phdrs.push_back(phdr);
			printf("Marking phdr to load: offs: 0x%08x, vaddr: 0x%08x, paddr: 0x%08x, flags: 0x%x\n", phdr.p_offset, phdr.p_vaddr, phdr.p_paddr, phdr.p_flags);
		}
	}

	uint32_t GetEntry()
	{
		return ehdr.e_entry;
	}

	uint8_t* GetPhdrBuf(int index)
	{
		if (index >= phdrs.size())
			return NULL;
		
		printf("Getting phdr at offset 0x%08x\n", phdrs[index].p_offset);
		return basePtr+phdrs[index].p_offset;
	}

	Elf32Phdr* GetPhdr(int index)
	{
		if (index >= phdrs.size())
			return NULL;
		
		return &phdrs[index];
	}

	int GetPhdrCount()
	{
		return phdrs.size();
	}
private:
	Elf32Header ehdr;
	std::vector<Elf32Phdr> phdrs;
	uint8_t* basePtr;
};

uint32_t CellSpurs::sysSpuImageImport(uint64_t imagePtr, uint32_t src, uint32_t size, uint32_t arg4, CellPPU* ppu)
{
	SpuElfLoader* loader = new SpuElfLoader(ppu->GetManager()->GetRawPtr(src));

	ppu->GetManager()->Write32(imagePtr+0x00, 0x00);
	ppu->GetManager()->Write32(imagePtr+0x04, loader->GetEntry());
	// This doesn't follow the struct layout, but it's convenient and no game should touch it anyway
	ppu->GetManager()->Write64(imagePtr+0x08, (uintptr_t)loader);

	printf("sysSpuImageImport(image=*0x%08lx, src=*0x%08x, size=0x%08x, arg4=0x%08x)\n", imagePtr, src, size, arg4);
	return CELL_OK;
}

uint32_t CellSpurs::sysSpuImageLoad(uint32_t spuIndex, uint64_t imagePtr, CellPPU *ppu)
{
	SpuElfLoader* elf = (SpuElfLoader*)((uintptr_t)ppu->GetManager()->Read64(imagePtr+0x08));

	int count = elf->GetPhdrCount();
	for (int i = 0; i < count; i++)
	{
		auto phdr = elf->GetPhdr(i);

		uint8_t* buf = elf->GetPhdrBuf(i);
		for (int i = 0; i < phdr->p_filesz; i++)
		{
			ppu->spus[spuIndex]->Write8(phdr->p_paddr+i, buf[i]);
		}
	}

	ppu->spus[spuIndex]->SetEntry(elf->GetEntry());

	printf("ELF Loaded\n");
	return CELL_OK;
}

uint32_t CellSpurs::sysSpuImageClose(uint64_t imagePtr, CellPPU *ppu)
{
	SpuElfLoader* elf = (SpuElfLoader*)((uintptr_t)ppu->GetManager()->Read64(imagePtr+0x08));

	delete elf;

	printf("sysSpuImageClose(image=*0x%08lx)\n", imagePtr);

	return CELL_OK;
}

uint32_t SpuThread::curId = 0x30;

int curGroupId = 20;
class SpuThreadGroup
{
public:
	SpuThreadGroup()
	{
		id = curGroupId++;
	}

	int id;
	int num;
	int priority;

	SpuThread* threads[6];
};

std::unordered_map<int, SpuThreadGroup*> spuGroups;
std::unordered_map<int, SpuThread*> spuThreads;

uint32_t CellSpurs::sysSpuThreadGroupCreate(uint64_t idPtr, int num, int prio, uint64_t attrPtr, CellPPU* ppu)
{
	SpuThreadGroup* group = new SpuThreadGroup();
	group->num = num;
	group->priority = prio;

	spuGroups[group->id] = group;

	ppu->GetManager()->Write32(idPtr, group->id);

	printf("sysSpuThreadGroupCreate(id=*0x%08lx, num=%d, prio=%d, attr=*0x%08lx)\n", idPtr, num, prio, attrPtr);
	return CELL_OK;
}

uint32_t CellSpurs::sysSpuThreadInitialize(uint64_t idPtr, uint32_t groupId, uint32_t spuNum, uint64_t imagePtr, uint64_t attrPtr, uint64_t argPtr, CellPPU *ppu)
{
	uint64_t namePtr = ppu->GetManager()->Read64(attrPtr);
	uint64_t nameLen = ppu->GetManager()->Read32(attrPtr+8);

	std::string name((const char*)ppu->GetManager()->GetRawPtr(namePtr), nameLen);

	printf("sysSpuThreadInitialize(id=*0x%08lx, group=%d, spuNum=%d, imagePtr=*0x%08lx, attrPtr=*0x%08lx, argPtr=*0x%08lx)\n", idPtr, groupId, spuNum, imagePtr, attrPtr, argPtr);

	SpuThreadGroup* group = spuGroups[groupId];

	if (!group || spuNum >= 6)
	{
		return CELL_EINVAL;
	}

	if (group->threads[spuNum] != NULL)
	{
		return CELL_EBUSY;
	}

	SpuThread* t = new SpuThread();
	t->id = SpuThread::curId++;

	SpuElfLoader* elf = (SpuElfLoader*)((uintptr_t)ppu->GetManager()->Read64(imagePtr+0x08));

	t->entry = elf->GetEntry();
	t->name = name;

	int count = elf->GetPhdrCount();
	for (int i = 0; i < count; i++)
	{
		auto phdr = elf->GetPhdr(i);

		uint8_t* buf = elf->GetPhdrBuf(i);
		for (int i = 0; i < phdr->p_filesz; i++)
		{
			ppu->spus[spuNum]->Write8(phdr->p_paddr+i, buf[i]);
		}
	}

	uint64_t arg0 = ppu->GetManager()->Read64(argPtr+0x00);
	uint64_t arg1 = ppu->GetManager()->Read64(argPtr+0x08);
	uint64_t arg2 = ppu->GetManager()->Read64(argPtr+0x10);
	uint64_t arg3 = ppu->GetManager()->Read64(argPtr+0x18);

	t->arg0 = arg0;
	t->arg1 = arg1;
	t->arg2 = arg2;
	t->arg3 = arg3;

	t->spu = spuNum;

	group->threads[spuNum] = t;
	spuThreads[t->id] = t;

	ppu->GetManager()->Write32(idPtr, t->id);

	return CELL_OK;
}

uint32_t CellSpurs::sysSpuThreadGroupStart(uint32_t groupId)
{
	SpuThreadGroup* group = spuGroups[groupId];

	if (!group)
	{
		return CELL_EINVAL;
	}

	for (int i = 0; i < 6; i++)
	{
		if (group->threads[i])
			g_spus[i]->SetThread(group->threads[i]);
	}

	return CELL_OK;
}

uint32_t CellSpurs::sysSpuThreadGroupJoin(uint32_t groupId, uint64_t causePtr, uint64_t statusPtr, CellPPU* ppu)
{
	if (!spuGroups.contains(groupId))
	{
		printf("WARNING: Invalid thread group id\n");
		return CELL_EINVAL;
	}

	SpuThreadGroup* group = spuGroups[groupId];

	for (int i = 0; i < 6; i++)
	{
		if (group->threads[i] && group->threads[i]->running)
		{
			printf("TODO: Putting PPU to sleep waiting on SPU\n");
			exit(1);
		}
	}

	ppu->GetManager()->Write32(causePtr, 1);
	ppu->GetManager()->Write32(statusPtr, 0);

	printf("sysSpuThreadGroupJoin(groupId=%d, cause=*0x%08lx, status=*0x%08lx)\n", groupId, causePtr, statusPtr);

	return CELL_OK;
}

uint32_t CellSpurs::sysSpuThreadSetSpuCfg(uint32_t threadId, uint64_t value)
{
	if (!spuThreads.contains(threadId))
	{
		printf("sysSpuThreadSetSpuCfg: Invalid thread ID\n");
		return CELL_EINVAL;
	}

	spuThreads[threadId]->cfg = value;

	printf("sysSpuThreadSetSpuCfg(threadId=*%d, value=*0x%08lx)\n", threadId, value);
	return CELL_OK;
}

uint32_t CellSpurs::sysSpuThreadWriteSnr(uint32_t id, int number, uint32_t value)
{
	assert(number == 0);

	if (!spuThreads.contains(id))
	{
		printf("sysSpuThreadWriteSnr: Invalid thread ID\n");
		return CELL_EINVAL;
	}

	int spuNum = spuThreads[id]->spu;
	g_spus[spuNum]->WriteProblemStorage(0x1400C, value);

	printf("sysSpuThreadWriteSnr(id=0x%08x, number=%d, value=0x%08x)\n", id, number, value);

	return CELL_OK;
}
