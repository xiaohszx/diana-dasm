#include "orthia.h"
#include "orthia_module_manager.h"
#include "orthia_memory_cache.h"
#include "diana_core_cpp.h"


class �WindbgMemoryReader:public orthia::IMemoryReader
{
public:
    virtual void Read(orthia::Address_type offset, 
                      orthia::Address_type bytesToRead,
                      void * pBuffer,
                      orthia::Address_type * pBytesRead,
                      int flags)
    {
        if (bytesToRead >= (orthia::Address_type)ULONG_MAX)
        {
            std::stringstream text;
            text<<"Range size is too big: "<<bytesToRead;
            throw std::runtime_error(text.str());
        }
        if (!(flags & ORTHIA_MR_FLAG_READ_THROUGH))
        {
            ULONG bytes = 0;
            ReadMemory(offset, 
                       pBuffer, 
                       (ULONG)bytesToRead,
                        &bytes);
            *pBytesRead = bytes;
            return;
        }
        // read through all valid pages 
        DbgExt_ReadThrough(offset, 
                           bytesToRead,
                           pBuffer);
        *pBytesRead = bytesToRead;
    }
};

struct ModuleManagerObjects
{
    �WindbgMemoryReader reader;
    orthia::CModuleManager moduleManager;
    ModuleManagerObjects()
    {
    }
    orthia::IMemoryReader * GetReader()
    {
        return &reader;
    }
};
static std::auto_ptr<ModuleManagerObjects> g_globals;


namespace orthia
{

static void PrintUsage()
{
    dprintf("!help - displays this text\n");
    dprintf("!profile [/f] <full_name> - uses/creates profile (f - force creation)\n");
    dprintf("!lm - displays module list\n");
    dprintf("!reload [/u] [/v] [/f] <module_address> - reloads module (u - unload, v - verbose, f - force)\n");
    dprintf("!x [/a] <address> - prints who calls to this address (a - print only addresses)\n");
    dprintf("!xr <address1> <address2> - prints who calls/jumps into this range \n");
    dprintf("!a <address> - analyzes the region\n");
}
static void SetupPath(const std::wstring & path, bool bForce)
{
    if (!g_globals.get())
    {
        g_globals.reset(new ModuleManagerObjects);
    }
    g_globals->moduleManager.Reinit(path, bForce);
}

orthia::IMemoryReader * QueryReader()
{
    ModuleManagerObjects * pGlobal = g_globals.get();
    return pGlobal->GetReader();
}
orthia::CModuleManager * QueryModuleManager()
{
    ModuleManagerObjects * pGlobal = g_globals.get();
    if (!pGlobal) 
    {
        throw std::runtime_error("Profile not inited");
    }
    return &pGlobal->moduleManager;
}

void InitLib()
{
    Diana_Init();
    try
    {
        // TODO: add initialization code here

    }
    catch(const std::exception & e)
    {
        &e;
        // do not care;
    }
}

} // orthia namespace

// commands
ORTHIA_DECLARE_API(help)
{
    dprintf("Orthia interface:\n\n");
    orthia::PrintUsage();
    return S_OK;
}

ORTHIA_DECLARE_API(profile)
{
    ORTHIA_CMD_START

        bool bForce = false;
        std::wstring wargs = orthia::ToString(orthia::Trim(args));
        if (wcsncmp(wargs.c_str(), L"/f ", 3) == 0)
        {
            bForce = true;
            wargs.erase(0, 3);
            wargs = orthia::Trim(wargs);
        }
        std::wstring path = orthia::ExpandVariable(wargs);
        orthia::SetupPath(path, bForce);

    ORTHIA_CMD_END
}

static std::wstring QueryModuleName(orthia::Address_type address,
                                    std::vector<char> & nameBuffer)
{
    ULONG64 displacement = 0;
    ULONG nameBufferSize = 0;
    nameBuffer.resize(4096);
    DbgExt_GetNameByOffset(address,
                           &nameBuffer.front(),
                           (ULONG)nameBuffer.size(),
                           &nameBufferSize,
                           &displacement);
    
    std::vector<std::string> args;
    args.clear();
    orthia::Split<char>(&nameBuffer.front(), &args, '!');

    std::string moduleName = args.empty()?"<unknown>":args[0];
    return orthia::ToString(moduleName);
}
static std::wstring QueryModuleName(orthia::Address_type address)
{
    std::vector<char> nameBuffer;
    return QueryModuleName(address, nameBuffer);
}

ORTHIA_DECLARE_API(lm)
{
    ORTHIA_CMD_START
           
    std::vector<std::string> args;
    std::vector<char> nameBuffer;
    orthia::CModuleManager * pModuleManager = orthia::QueryModuleManager();
    std::vector<orthia::CommonModuleInfo> modules;
    pModuleManager->QueryLoadedModules(&modules);
    for(std::vector<orthia::CommonModuleInfo>::iterator it = modules.begin(), it_end = modules.end();
        it != it_end;
        ++it)
    {
        
        dprintf("%I64lx     %S\n", it->address, it->name.c_str());
    }
    ORTHIA_CMD_END
}

ORTHIA_DECLARE_API(reload)
{
    ORTHIA_CMD_START
        
    std::vector<std::wstring> words;
    orthia::Split(orthia::ToString(args), &words);
    bool bUnload = false;
    orthia::Address_type offset = 0;
    bool offsetInited = false;
    bool bForce = false;
    bool bVerbose = false;
    for(std::vector<std::wstring>::iterator it = words.begin(), it_end = words.end();
        it != it_end;
        ++it)
    {
        if (*it== L"/u")
        {
            bUnload = true;
            continue;
        }
        if (*it== L"/f")
        {
            bForce = true;
            continue;
        }
        if (*it== L"/v")
        {
            bVerbose = true;
            continue;
        }
        if (offsetInited)
            throw std::runtime_error("Unexpected argument: " + orthia::ToAnsiString_Silent(*it));
        PCSTR tail = 0;
        std::string strOffset = orthia::ToAnsiString_Silent(*it);
        GetExpressionEx(strOffset.c_str(), &offset, &tail);
        offsetInited = true;
    }
    
    orthia::CModuleManager * pModuleManager = orthia::QueryModuleManager();
    if (bUnload)
    {
        pModuleManager->UnloadModule(offset);
        if (bVerbose)
        {
            dprintf("%s %I64lx \n", "Module unloaded: ", offset);
        }
        return S_OK;
    }
    std::wstring name = QueryModuleName(offset);
    pModuleManager->ReloadModule(offset, orthia::QueryReader(), bForce, name);
    if (bVerbose)    
    {        
        dprintf("%s %I64lx  %S\n", "Module loaded: ", offset, name.c_str());
    }
    ORTHIA_CMD_END
}

ORTHIA_DECLARE_API(xr)
{
    ORTHIA_CMD_START

    std::vector<std::string> words;
    orthia::Split<char>(args, &words);

    if (words.size() != 2)
    {
        throw std::runtime_error("Two arguments expected");
    }

    orthia::Address_type address1 = 0, address2 = 0;
    PCSTR tail = 0;
    if (!GetExpressionEx(words[0].c_str(), &address1, &tail))
    {
        throw std::runtime_error("Invalid argument1: " + words[0]);
    }
    if (!GetExpressionEx(words[1].c_str(), &address2, &tail))
    {
        throw std::runtime_error("Invalid argument2: " + words[1]);
    }

    orthia::CModuleManager * pModuleManager = orthia::QueryModuleManager();

    std::vector<orthia::CommonRangeInfo> references;
    pModuleManager->QueryReferencesToInstructionsRange(address1, address2, &references);

    std::vector<char> nameBuffer;
    for(std::vector<orthia::CommonRangeInfo>::iterator it = references.begin(), it_end = references.end();
        it != it_end;
        ++it)
    {
        dprintf("%I64lx:\n", it->address);
        
        for(std::vector<orthia::CommonReferenceInfo>::iterator it2 = it->references.begin(), it2_end = it->references.end();
            it2 != it2_end;
            ++it2)
        {
            ULONG64 displacement = 0;
            ULONG nameBufferSize = 0;
            nameBuffer.resize(4096);
            DbgExt_GetNameByOffset(it2->address,
                                   &nameBuffer.front(),
                                   (ULONG)nameBuffer.size(),
                                   &nameBufferSize,
                                   &displacement);

            if (displacement)
            {
                dprintf("    %I64lx     %s+%I64lx\n", it2->address, &nameBuffer.front(), displacement);
            }
            else
            {
                dprintf("    %I64lx     %s\n", it2->address, &nameBuffer.front());
            }
        }
    }   

    ORTHIA_CMD_END
}
ORTHIA_DECLARE_API(x)
{
    ORTHIA_CMD_START

    std::vector<std::wstring> words;
    orthia::Split(orthia::ToString(args), &words);

    orthia::Address_type address = 0;
    bool addressInited = false;
    bool bDisplaySymbols = true;
    for(std::vector<std::wstring>::iterator it = words.begin(), it_end = words.end();
        it != it_end;
        ++it)
    {
        if (*it== L"/a")
        {
            bDisplaySymbols = false;
            continue;
        }
        if (addressInited)
            throw std::runtime_error("Unexpected argument: " + orthia::ToAnsiString_Silent(*it));
        PCSTR tail = 0;
        std::string strOffset = orthia::ToAnsiString_Silent(*it);
        if (!GetExpressionEx(strOffset.c_str(), &address, &tail))
        {
            throw std::runtime_error("Address expression expected");
        }
        addressInited = true;
    }

    orthia::CModuleManager * pModuleManager = orthia::QueryModuleManager();

    std::vector<orthia::CommonReferenceInfo> references;
    pModuleManager->QueryReferencesToInstruction(address, &references);

    std::vector<char> nameBuffer;
    for(std::vector<orthia::CommonReferenceInfo>::iterator it = references.begin(), it_end = references.end();
        it != it_end;
        ++it)
    {
        if (bDisplaySymbols)
        {
            ULONG64 displacement = 0;
            ULONG nameBufferSize = 0;
            nameBuffer.resize(4096);
            DbgExt_GetNameByOffset(it->address,
                                   &nameBuffer.front(),
                                   (ULONG)nameBuffer.size(),
                                   &nameBufferSize,
                                   &displacement);

            if (displacement)
            {
                dprintf("%I64lx     %s+%I64lx\n", it->address, &nameBuffer.front(), displacement);
            }
            else
            {
                dprintf("%I64lx     %s\n", it->address, &nameBuffer.front());
            }
        }
        else
        {
            dprintf("%I64lx\n", it->address);
        }
    }   
    ORTHIA_CMD_END
}


ORTHIA_DECLARE_API(a)
{
    ORTHIA_CMD_START

    std::vector<std::string> words;
    orthia::Split<char>(args, &words);

    if (words.size() != 1)
    {
        throw std::runtime_error("One argument expected");
    }

    orthia::Address_type address = 0;
    PCSTR tail = 0;
    if (!GetExpressionEx(words[0].c_str(), &address, &tail))
    {
        throw std::runtime_error("Invalid argument: " + words[0]);
    }

    orthia::CModuleManager * pModuleManager = orthia::QueryModuleManager();
    
    orthia::Address_type size = DbgExt_GetRegionSize(address);
    if (!size)
    {
        throw std::runtime_error("Invalid region: " + words[0]);
    }
    ULONG machine = DbgExt_GetTargetMachine();
    int mode = 0;
    switch(machine)
    {
    case IMAGE_FILE_MACHINE_I386:
        mode = DIANA_MODE32;
        break;
    case IMAGE_FILE_MACHINE_AMD64:
        mode = DIANA_MODE64;
        break;
    default:
        {
            std::stringstream errorStream;
            errorStream<<"Unsupported target: "<<machine;
            throw std::runtime_error(errorStream.str());
        }
    }
    pModuleManager->ReloadRange(address, size, orthia::QueryReader(), mode);

    ORTHIA_CMD_END
}
