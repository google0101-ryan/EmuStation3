#include "CellSpurs.h"
#include "util.h"
#include <string.h>
#include <cpu/PPU.h>
#include <stdexcept>
#include <cassert>
#include <fstream>

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



uint32_t SpuThread::curId = 0x30;

int curGroupId = 20;
class SpuThreadGroup
{
public:
	SpuThreadGroup()
	{
		id = curGroupId++;
		memset(threads, 0, sizeof(threads));
	}

	int id;
	int num;
	int priority;

	SpuThread* threads[6];
};

std::unordered_map<int, SpuThreadGroup*> spuGroups;
std::unordered_map<int, SpuThread*> spuThreads;

uint32_t CellSpu::sysSpuThreadGroupCreate(uint64_t idPtr, int num, int prio, uint64_t attrPtr, CellPPU* ppu)
{
	SpuThreadGroup* group = new SpuThreadGroup();
	group->num = num;
	group->priority = prio;

	spuGroups[group->id] = group;

	ppu->GetManager()->Write32(idPtr, group->id);

	printf("sysSpuThreadGroupCreate(id=*0x%08lx, num=%d, prio=%d, attr=*0x%08lx)\n", idPtr, num, prio, attrPtr);
	return CELL_OK;
}

bool sys_process_is_spu_lockline(uint32_t addr, uint32_t flags)
{
	if (!flags || flags & ~3)
	{
		return CELL_EINVAL;
	}

	switch (addr >> 28)
	{
	case 0x0:
	case 0x1:
	case 0x2:
	case 0xc:
	case 0xe:
		break;
	case 0xf:
	{
		if (flags & 1)
			return CELL_EPERM;
		break;
	}
	case 0xd:
		return CELL_EPERM;
	default:
		return CELL_EINVAL;
	}

	return CELL_OK;
}

SpuThreadGroup* spursThreadGroup;

uint32_t initSpurs(uint32_t spursAddr, uint32_t revision, uint32_t sdkVersion, int nSpus, int spuPriority, int ppuPriority, uint32_t flags, char* prefix, CellPPU* ppu)
{
	if (!spursAddr)
	{
		return 0x80410711;
	}

	if ((spursAddr & 0x7f) != 0)
	{
		return 0x80410710;
	}

	if (sys_process_is_spu_lockline(spursAddr, 2) != 0)
	{
		return 0x80410709;
	}

	bool isVer1 = !(flags & 4);

	if (isVer1)
		memset(ppu->GetManager()->GetRawPtr(spursAddr), 0, 0x1000);
	else
		memset(ppu->GetManager()->GetRawPtr(spursAddr), 0, 0x2000);
	
	ppu->GetManager()->Write32(spursAddr+0xda0, revision);
	ppu->GetManager()->Write32(spursAddr+0xda4, sdkVersion);
	ppu->GetManager()->Write64(spursAddr+0xd20, 0xffffffff);
	ppu->GetManager()->Write64(spursAddr+0xd28, 0xffffffff);
	ppu->GetManager()->Write32(spursAddr+0xd80, flags);
	if (prefix)
	{
		memcpy(ppu->GetManager()->GetRawPtr(spursAddr+0xd8c), prefix, 15);
	}
	if (isVer1)
	{
		ppu->GetManager()->Write32(spursAddr+0xb0, 0xffff);
	}
	ppu->GetManager()->Write8(spursAddr+0xce, 0);
	ppu->GetManager()->Write8(spursAddr+0xcc, 0);
	ppu->GetManager()->Write8(spursAddr+0xcd, 0);
	for (int i = 0; i < 8; i++)
	{
		ppu->GetManager()->Write8(spursAddr+0xc0+i, 0xff);
	}
	ppu->GetManager()->Write64(spursAddr+0xd00, 0x100);
	ppu->GetManager()->Write64(spursAddr+0xd08, 0);
	ppu->GetManager()->Write32(spursAddr+0xd10, 0x2200);
	ppu->GetManager()->Write8(spursAddr+0xd14, 0xff);
	for (int i = 0; i < 16; i++)
	{
		// TODO: Create semas and assign them here
	}
	if ((flags & 4) != 0)
	{
		printf("TODO: Spurs2\n");
		exit(1);
	}
	// TODO: Create srv sema and assign it here
	ppu->GetManager()->Write32(spursAddr + 0x98c, 0xffffffff);
	ppu->GetManager()->Write64(spursAddr + 0x990, 0);
	ppu->GetManager()->Write32(spursAddr + 0x988, 0xffffffff);
	ppu->GetManager()->Write8(spursAddr + 0x76, nSpus);
	ppu->GetManager()->Write32(spursAddr + 0xd84, spuPriority);

	std::ifstream file("spukernel.elf", std::ios::ate | std::ios::binary);
	size_t size = file.tellg();
	file.seekg(0, std::ios::beg);
	uint8_t* buf = new uint8_t[size];
	file.read((char*)buf, size);
	file.close();
	SpuElfLoader loader(buf);

	spursThreadGroup = new SpuThreadGroup();
	spuGroups[spursThreadGroup->id] = spursThreadGroup;

	for (int i = 0; i < nSpus; i++)
	{
		spursThreadGroup->threads[i] = new SpuThread();
		auto& t = spursThreadGroup->threads[i];
		t->arg0 = (uint64_t)i << 0x20;
		t->arg1 = spursAddr;
		t->entry = loader.GetEntry();

		// Load the kernel
		int count = loader.GetPhdrCount();
		for (int j = 0; j < count; j++)
		{
			auto phdr = loader.GetPhdr(j);

			uint8_t* buf = loader.GetPhdrBuf(j);
			for (int k = 0; k < phdr->p_filesz; k++)
			{
				ppu->spus[i]->Write8(phdr->p_paddr+k, buf[k]);
			}
		}
		
		ppu->spus[i]->SetThread(spursThreadGroup->threads[i]);
	}
	
	return CELL_OK;
}

uint32_t CellSpu::cellSpursAttributeInitialize(uint32_t attrPtr, uint32_t revision, 
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

uint32_t CellSpu::cellSpursAttributeEnableSpuPrintfIfAvailable(uint32_t attrPtr, CellPPU* ppu)
{
	printf("cellSpursAttributeEnableSpuPrintfIfAvailable(attr=*0x%08x)\n", attrPtr);
	ppu->GetManager()->Write32(attrPtr+0x28, ppu->GetManager()->Read32(attrPtr+0x28) | 0x10000000);
	return CELL_OK;
}

uint32_t CellSpu::cellSpursAttributeSetNamePrefix(uint32_t attrPtr, uint32_t namePtr, uint64_t size, CellPPU* ppu)
{
	printf("cellSpursAttributeSetNamePrefix(0x%08x, %s)\n", attrPtr, ppu->GetManager()->GetRawPtr(namePtr));
	memcpy(ppu->GetManager()->GetRawPtr(attrPtr+0x15), ppu->GetManager()->GetRawPtr(namePtr), size);

	return CELL_OK;
}

uint32_t CellSpu::cellSpursInitializeWithAttribute2(uint32_t spursPtr, uint32_t attrPtr, CellPPU *ppu)
{
	printf("cellSpursInitializeWithAttribute2(0x%08x, 0x%08x)\n", spursPtr, attrPtr);

	// TODO: Write out the massive structure that holds all SPURS information

    return CELL_OK;
}

uint32_t CellSpu::cellSpursTasksetAttributeInitialize(uint32_t attrPtr, uint32_t revision, uint32_t sdkVersion, uint64_t args, uint32_t priorityPtr, uint32_t maxContention, CellPPU *ppu)
{
	printf("cellSpursTasksetAttributeInitialize(0x%08x, 0x%08x, 0x%08x, 0x%08lx, 0x%08x, 0x%08x)\n", attrPtr, revision, sdkVersion, args, priorityPtr);

	if (!attrPtr)
	{
		return 0x80410911;
	}

	uint8_t priority[8];

	for (uint32_t i = 0; i < 8; i++)
	{
		priority[i] = ppu->GetManager()->Read8(priorityPtr);
		if (priority[i] > 0xF)
		{
			return 0x80410902;
		}
	}

	ppu->GetManager()->Write32(attrPtr+0x00, revision);
	ppu->GetManager()->Write32(attrPtr+0x04, sdkVersion);
	ppu->GetManager()->Write32(attrPtr+0x08, args);
	ppu->GetManager()->Write32(attrPtr+0x20, 6400);
	ppu->GetManager()->Write32(attrPtr+0x18, maxContention);
	memcpy(ppu->GetManager()->GetRawPtr(attrPtr+0x10), priority, 8);

    return CELL_OK;
}

uint32_t CellSpu::cellSpursTasksetAttributeSetName(uint32_t attrPtr, uint32_t namePtr, CellPPU* ppu)
{
	const char* name = (const char*)ppu->GetManager()->GetRawPtr(namePtr);

	printf("cellSpursTasksetAttributeSetName(0x%08x, \"%s\")\n", attrPtr, name);

	ppu->GetManager()->Write32(attrPtr+0x1C, namePtr);

    return CELL_OK;
}

uint32_t CellSpu::cellSpursInitialize(uint32_t spursPtr, int nSpus, int spuPriority, int ppuPriority, bool exitIfNoWork, CellPPU* ppu)
{
	return initSpurs(spursPtr, 0, 0, nSpus, spuPriority, ppuPriority, 0, NULL, ppu);
}

uint32_t sys_spu_thread_group_connect_event_all_threads(uint32_t id, uint32_t eq, uint64_t req, uint32_t spuPtr, CellPPU* ppu)
{
	printf("sys_spu_thread_group_connect_event_all_threads(%d, %d, 0x%08lx, 0x%08x)\n", id, eq, req, spuPtr);

	uint8_t port = 0;

	if (!spuGroups[id])
	{
		printf("Tried to connect event to non-existant thread queue");
		exit(1);
	}

	auto& group = spuGroups[id];

	for (; port < 64; port++)
	{
		if (!(req & (1ull << port)))
		{
			continue;
		}

		bool found = true;
		for (int i = 0; i < 6; i++)
		{
			auto& t = group->threads[i];

			if (t)
			{
				if (t->spup[port] != 0)
				{
					found = false;
					break;
				}
			}
		}

		if (found)
		{
			break;
		}
	}

	if (port == 64)
	{
		printf("CELL_EISCONN\n");
		exit(1);
	}

	for (int i = 0; i < 6; i++)
	{
		auto& t = group->threads[i];
		if (t)
		{
			t->spup[port] = eq;
		}
	}

	if (spuPtr)
	{
		ppu->GetManager()->Write8(spuPtr, port);
	}

	return CELL_OK;
}

uint32_t CellSpu::cellSpursAttachLV2EventQueue(uint32_t spursPtr, uint32_t queueId, uint32_t portPtr, int isDynamic, CellPPU* ppu)
{
	printf("cellSpursAttachLV2EventQueue(0x%08x, %d, 0x%08x, %d)\n", spursPtr, queueId, portPtr, isDynamic);

	if (!spursPtr || !portPtr)
	{
		return 0x80410711;
	}

	if ((spursPtr & 0x7f) != 0)
	{
		return 0x80410710;
	}

	uint8_t _port = 0x3f;
	uint64_t portMask = 0;

	if (isDynamic == 0)
	{
		_port = ppu->GetManager()->Read8(portPtr);
		if (_port > 0x3f)
		{
			return 0x80410702;
		}
	}

	for (uint32_t i = isDynamic ? 0x10 : _port; i <= _port; i++)
	{
		portMask |= 1ull << (i);
	}

	sys_spu_thread_group_connect_event_all_threads(spursThreadGroup->id, queueId, portMask, portPtr, ppu);

	uint64_t portBits = ppu->GetManager()->Read32(spursPtr+0xda8);
	ppu->GetManager()->Write32(spursPtr+0xda8, portBits | (1ull << ppu->GetManager()->Read8(portPtr)));

	return CELL_OK;
}

uint32_t CellSpu::cellSpursGetInfo(uint32_t spurs, uint32_t info, CellPPU* ppu)
{
	printf("cellSpursGetInfo(0x%08x, 0x%08x)\n", spurs, info);

	if (!spurs || !info)
	{
		return 0x80410711;
	}

	if ((spurs & 0x7f) != 0)
	{
		return 0x80410710;
	}

	ppu->GetManager()->Write32(info+0x00, ppu->GetManager()->Read32(spurs+0x76));
	ppu->GetManager()->Write32(info+0x04, ppu->GetManager()->Read32(spurs+0xd84));
	ppu->GetManager()->Write32(info+0x08, ppu->GetManager()->Read32(spurs+0xd88));
	ppu->GetManager()->Write32(info+0x24, ppu->GetManager()->Read32(spurs+0xd30));
	ppu->GetManager()->Write32(info+0x28, ppu->GetManager()->Read32(spurs+0xd34));
	ppu->GetManager()->Write8(info+0x0C, ppu->GetManager()->Read32(spurs+0xd80) & 1);
	ppu->GetManager()->Write32(info+0x2C, ppu->GetManager()->Read32(spurs+0xd38));
	ppu->GetManager()->Write8(info+0xd, (ppu->GetManager()->Read32(spurs+0xd80) >> 2) & 1);
	ppu->GetManager()->Write32(info+0x30, ppu->GetManager()->Read32(spurs+0xd3C));
	ppu->GetManager()->Write32(info+0x34, ppu->GetManager()->Read32(spurs+0xd40));
	ppu->GetManager()->Write32(info+0x38, ppu->GetManager()->Read32(spurs+0xd44));
	ppu->GetManager()->Write32(info+0x3C, ppu->GetManager()->Read32(spurs+0xd48));
	ppu->GetManager()->Write32(info+0x40, ppu->GetManager()->Read32(spurs+0xd4c));
	ppu->GetManager()->Write64(info+0x48, ppu->GetManager()->Read64(spurs+0xd20));
	ppu->GetManager()->Write32(info+0x44, ppu->GetManager()->Read32(spurs+0xd50));
	ppu->GetManager()->Write64(info+0x50, ppu->GetManager()->Read64(spurs+0xd28));
	ppu->GetManager()->Write32(info+0x10, ppu->GetManager()->Read64(spurs+0x900) & 0xfffffffc);
	ppu->GetManager()->Write64(info+0x18, ppu->GetManager()->Read64(spurs+0x948));
	ppu->GetManager()->Write32(info+0x20, ppu->GetManager()->Read64(spurs+0x900) & 3);
	memcpy(ppu->GetManager()->GetRawPtr(info+0x58), ppu->GetManager()->GetRawPtr(spurs+0xd8c), 15);
	ppu->GetManager()->Write32(info+0x68, ppu->GetManager()->Read32(spurs+0xd9b));

	return CELL_OK;	
}

uint32_t CellSpu::sysSpuImageImport(uint64_t imagePtr, uint32_t src, uint32_t size, uint32_t arg4, CellPPU* ppu)
{
	SpuElfLoader* loader = new SpuElfLoader(ppu->GetManager()->GetRawPtr(src));

	ppu->GetManager()->Write32(imagePtr+0x00, 0x00);
	ppu->GetManager()->Write32(imagePtr+0x04, loader->GetEntry());
	// This doesn't follow the struct layout, but it's convenient and no game should touch it anyway
	ppu->GetManager()->Write64(imagePtr+0x08, (uintptr_t)loader);

	printf("sysSpuImageImport(image=*0x%08lx, src=*0x%08x, size=0x%08x, arg4=0x%08x)\n", imagePtr, src, size, arg4);
	return CELL_OK;
}

uint32_t CellSpu::sysSpuImageLoad(uint32_t spuIndex, uint64_t imagePtr, CellPPU *ppu)
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

uint32_t CellSpu::sysSpuImageClose(uint64_t imagePtr, CellPPU *ppu)
{
	SpuElfLoader* elf = (SpuElfLoader*)((uintptr_t)ppu->GetManager()->Read64(imagePtr+0x08));

	delete elf;

	printf("sysSpuImageClose(image=*0x%08lx)\n", imagePtr);

	return CELL_OK;
}

uint32_t CellSpu::sysSpuThreadInitialize(uint64_t idPtr, uint32_t groupId, uint32_t spuNum, uint64_t imagePtr, uint64_t attrPtr, uint64_t argPtr, CellPPU *ppu)
{
	uint64_t namePtr = ppu->GetManager()->Read32(attrPtr);
	uint64_t nameLen = ppu->GetManager()->Read32(attrPtr+4);

	std::string name((const char*)ppu->GetManager()->GetRawPtr(namePtr), nameLen);

	printf("sysSpuThreadInitialize(id=*0x%08lx, group=%d, spuNum=%d, imagePtr=*0x%08lx, attrPtr=*0x%08lx, argPtr=*0x%08lx)\n", idPtr, groupId, spuNum, imagePtr, attrPtr, argPtr);
	printf("name = %s\n", name.c_str());

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
		for (int j = 0; j < phdr->p_filesz; j++)
		{
			ppu->spus[spuNum]->Write8(phdr->p_paddr+j, buf[j]);
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

uint32_t CellSpu::sysSpuThreadGroupStart(uint32_t groupId)
{
	SpuThreadGroup* group = spuGroups[groupId];

	printf("Found group with id=%d\n", group->id);

	if (!group)
	{
		return CELL_EINVAL;
	}

	for (int i = 0; i < 6; i++)
	{
		printf("%p\n", group->threads[i]);
		if (group->threads[i])
			g_spus[i]->SetThread(group->threads[i]);
	}

	return CELL_OK;
}

uint32_t CellSpu::sysSpuThreadGroupJoin(uint32_t groupId, uint64_t causePtr, uint64_t statusPtr, CellPPU* ppu)
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

uint32_t CellSpu::sysSpuThreadSetSpuCfg(uint32_t threadId, uint64_t value)
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

uint32_t CellSpu::sysSpuThreadWriteSnr(uint32_t id, int number, uint32_t value)
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
