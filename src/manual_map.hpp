#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>
#include "shared.hpp"

// Loader for this project's x64 MinGW DLL. It changes section protections
// in its allocation and removes its PE header after initialization.
// The runtime hook engine separately patches selected game code pages.
namespace manual_map {
using Address=std::uintptr_t;
inline void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
inline Address moduleBase(DWORD pid,const wchar_t* name){
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(snapshot==INVALID_HANDLE_VALUE)return 0;
    MODULEENTRY32W item{};item.dwSize=sizeof(item);Address result=0;
    if(Module32FirstW(snapshot,&item))do{if(!_wcsicmp(item.szModule,name)){result=reinterpret_cast<Address>(item.modBaseAddr);break;}}while(Module32NextW(snapshot,&item));
    CloseHandle(snapshot);return result;
}
inline Address invoke(HANDLE process,Address fn,Address a,Address b,Address c,Address d);
inline Address remoteFunction(DWORD pid,FARPROC fn,HANDLE process=nullptr){
    HMODULE owner=nullptr;
    require(fn&&GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(fn),&owner),"Nie mozna ustalic modulu importu.");
    wchar_t path[32768]{};require(GetModuleFileNameW(owner,path,32768)!=0,"Brak nazwy modulu importu.");
    auto name=std::filesystem::path(path).filename().wstring();auto base=moduleBase(pid,name.c_str());
    if(!base&&process){
        wchar_t systemDirectory[32768]{};require(GetSystemDirectoryW(systemDirectory,32768)!=0,"Brak sciezki System32.");
        auto parent=std::filesystem::path(path).parent_path().wstring();
        require(!_wcsicmp(parent.c_str(),systemDirectory),"Brak zaleznosci spoza katalogu systemowego.");
        size_t bytes=(wcslen(path)+1)*sizeof(wchar_t);auto buffer=VirtualAllocEx(process,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        require(buffer!=nullptr,"Brak pamieci na sciezke biblioteki systemowej.");SIZE_T written=0;
        if(!WriteProcessMemory(process,buffer,path,bytes,&written)||written!=bytes){VirtualFreeEx(process,buffer,0,MEM_RELEASE);throw std::runtime_error("Blad zapisu sciezki biblioteki systemowej.");}
        // A timeout retains the string buffer, because LoadLibrary may still use it.
        auto load=remoteFunction(pid,GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"));
        auto loaded=invoke(process,load,reinterpret_cast<Address>(buffer),0,0,0);
        VirtualFreeEx(process,buffer,0,MEM_RELEASE);require(loaded!=0,"Nie mozna zaladowac biblioteki systemowej.");base=moduleBase(pid,name.c_str());
    }
    require(base!=0,"Biblioteka importu nie jest zaladowana w procesie docelowym.");
    return base+reinterpret_cast<Address>(fn)-reinterpret_cast<Address>(owner);
}
inline void write(HANDLE process,Address dest,const void* data,size_t length){
    SIZE_T written=0;require(WriteProcessMemory(process,reinterpret_cast<void*>(dest),data,length,&written)&&written==length,"Blad zapisu obrazu DLL.");
}
// A bounded x64 call gate: four integer/pointer arguments, 32-byte shadow space.
// Only called with normal Win64 ABI functions; never used for game methods.
inline Address invoke(HANDLE process,Address fn,Address a=0,Address b=0,Address c=0,Address d=0){
    struct Call {Address fn,a,b,c,d,result;} input{fn,a,b,c,d,0};
    const unsigned char gate[]={
        0x53,                         // push rbx
        0x48,0x83,0xec,0x20,           // sub rsp,32
        0x48,0x89,0xcb,                // mov rbx,rcx
        0x48,0x8b,0x03,                // mov rax,[rbx]
        0x48,0x8b,0x4b,0x08,          // mov rcx,[rbx+8]
        0x48,0x8b,0x53,0x10,          // mov rdx,[rbx+16]
        0x4c,0x8b,0x43,0x18,          // mov r8,[rbx+24]
        0x4c,0x8b,0x4b,0x20,          // mov r9,[rbx+32]
        0xff,0xd0,                    // call rax
        0x48,0x89,0x43,0x28,          // mov [rbx+40],rax
        0x48,0x83,0xc4,0x20,0x5b,0x31,0xc0,0xc3
    };
    auto mem=reinterpret_cast<Address>(VirtualAllocEx(process,nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    require(mem!=0,"Brak pamieci dla wywolania loadera.");HANDLE thread=nullptr;bool running=false;
    try{
        write(process,mem,gate,sizeof(gate));write(process,mem+4096,&input,sizeof(input));DWORD old=0;
        require(VirtualProtectEx(process,reinterpret_cast<void*>(mem),4096,PAGE_EXECUTE_READ,&old)!=0,"Nie mozna ustawic ochrony kodu loadera.");
        FlushInstructionCache(process,reinterpret_cast<void*>(mem),sizeof(gate));
        thread=CreateRemoteThread(process,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(mem),reinterpret_cast<void*>(mem+4096),0,nullptr);
        require(thread!=nullptr,"Nie mozna uruchomic loadera.");running=true;
        require(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0,"Timeout loadera; zachowano jego pamiec do zakonczenia procesu.");running=false;
        DWORD exitCode=0;require(GetExitCodeThread(thread,&exitCode)&&exitCode==0,"Wywolanie loadera zakonczylo sie wyjatkiem.");
        SIZE_T read=0;require(ReadProcessMemory(process,reinterpret_cast<void*>(mem+4096),&input,sizeof(input),&read)&&read==sizeof(input),"Brak wyniku loadera.");
        CloseHandle(thread);VirtualFreeEx(process,reinterpret_cast<void*>(mem),0,MEM_RELEASE);return input.result;
    }catch(...){if(thread)CloseHandle(thread);if(!running)VirtualFreeEx(process,reinterpret_cast<void*>(mem),0,MEM_RELEASE);throw;}
}
inline Address load(HANDLE process,DWORD pid,DWORD threadId,const std::wstring& filename,bool testOnly=false){
    std::ifstream stream(std::filesystem::path(filename),std::ios::binary|std::ios::ate);
    require(stream.good(),"Nie mozna otworzyc ScarletAim.dll.");auto length=stream.tellg();
    require(length>0&&length<64*1024*1024,"Nieprawidlowy rozmiar DLL.");std::vector<unsigned char> raw(static_cast<size_t>(length));stream.seekg(0);stream.read(reinterpret_cast<char*>(raw.data()),length);
    require(stream.good()&&raw.size()>=sizeof(IMAGE_DOS_HEADER),"Niepelny plik DLL.");
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    require(dos->e_magic==IMAGE_DOS_SIGNATURE&&dos->e_lfanew>0&&static_cast<size_t>(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS64)<=raw.size(),"Nieprawidlowy naglowek DOS.");
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(raw.data()+dos->e_lfanew);
    require(nt->Signature==IMAGE_NT_SIGNATURE&&nt->FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64&&nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC&&(nt->FileHeader.Characteristics&IMAGE_FILE_DLL),"Wymagana biblioteka PE x64.");
    auto opt=nt->OptionalHeader;
    require(opt.SizeOfImage>0&&opt.SizeOfImage<64*1024*1024&&opt.SizeOfHeaders<=raw.size()&&opt.SizeOfHeaders<=opt.SizeOfImage,"Nieprawidlowe rozmiary obrazu.");
    require(!opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size,"Importy opoznione nie sa obslugiwane.");
    std::vector<unsigned char> image(opt.SizeOfImage);std::memcpy(image.data(),raw.data(),opt.SizeOfHeaders);
    auto section=IMAGE_FIRST_SECTION(nt);
    require(reinterpret_cast<const unsigned char*>(section+nt->FileHeader.NumberOfSections)<=raw.data()+raw.size(),"Niepelna tablica sekcji.");
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=section[i];
        require(static_cast<size_t>(s.PointerToRawData)+s.SizeOfRawData<=raw.size()&&static_cast<size_t>(s.VirtualAddress)+std::max(s.SizeOfRawData,s.Misc.VirtualSize)<=image.size(),"Sekcja poza obrazem DLL.");
        if(s.SizeOfRawData)std::memcpy(image.data()+s.VirtualAddress,raw.data()+s.PointerToRawData,s.SizeOfRawData);
    }
    auto at=[&](size_t rva,size_t size)->unsigned char*{require(rva<=image.size()&&size<=image.size()-rva,"RVA poza obrazem DLL.");return image.data()+rva;};
    auto str=[&](size_t rva)->const char*{auto p=reinterpret_cast<const char*>(at(rva,1));require(std::memchr(p,0,image.size()-rva)!=nullptr,"Nieprawidlowa nazwa importu.");return p;};
    auto expDir=opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];require(expDir.Size!=0,"Brak eksportu bootstrap.");
    auto exports=reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(at(expDir.VirtualAddress,sizeof(IMAGE_EXPORT_DIRECTORY)));DWORD bootstrap=0;
    auto names=reinterpret_cast<DWORD*>(at(exports->AddressOfNames,static_cast<size_t>(exports->NumberOfNames)*4));
    auto ords=reinterpret_cast<WORD*>(at(exports->AddressOfNameOrdinals,static_cast<size_t>(exports->NumberOfNames)*2));
    auto funcs=reinterpret_cast<DWORD*>(at(exports->AddressOfFunctions,static_cast<size_t>(exports->NumberOfFunctions)*4));
    for(DWORD i=0;i<exports->NumberOfNames;++i)if(!std::strcmp(str(names[i]),"MapStart")){require(ords[i]<exports->NumberOfFunctions,"Nieprawidlowy ordinal eksportu.");bootstrap=funcs[ords[i]];}
    require(bootstrap&&bootstrap<image.size(),"DLL nie zawiera MapStart; uzyj aktualnego ScarletAim.dll.");
    auto tlsDir=opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if(tlsDir.Size){auto tls=reinterpret_cast<IMAGE_TLS_DIRECTORY64*>(at(tlsDir.VirtualAddress,sizeof(IMAGE_TLS_DIRECTORY64)));
        require(tls->EndAddressOfRawData>=tls->StartAddressOfRawData&&tls->EndAddressOfRawData-tls->StartAddressOfRawData<=8&&tls->SizeOfZeroFill==0,"Ta DLL wymaga statycznego TLS; uzyj zwyklego loadera.");
        auto templateSize=static_cast<size_t>(tls->EndAddressOfRawData-tls->StartAddressOfRawData);
        auto bytes=at(static_cast<size_t>(tls->StartAddressOfRawData-opt.ImageBase),templateSize);
        for(size_t i=0;i<templateSize;++i)require(bytes[i]==0,"Niepusty statyczny TLS nie jest obslugiwany.");
    }
    Address base=reinterpret_cast<Address>(VirtualAllocEx(process,reinterpret_cast<void*>(opt.ImageBase),image.size(),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!base)base=reinterpret_cast<Address>(VirtualAllocEx(process,nullptr,image.size(),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    require(base!=0,"Nie mozna przydzielic pamieci DLL.");bool attached=false,registered=false;Address bootstrapData=0;
    try{
        auto reloc=opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];Address delta=base-opt.ImageBase;
        if(delta){require(reloc.Size!=0,"DLL nie ma relokacji.");size_t offset=0;
            while(offset<reloc.Size){auto block=reinterpret_cast<IMAGE_BASE_RELOCATION*>(at(reloc.VirtualAddress+offset,sizeof(IMAGE_BASE_RELOCATION)));
                require(block->SizeOfBlock>=sizeof(*block)&&block->SizeOfBlock<=reloc.Size-offset,"Nieprawidlowa relokacja.");
                size_t n=(block->SizeOfBlock-sizeof(*block))/2;auto entries=reinterpret_cast<WORD*>(at(reloc.VirtualAddress+offset+sizeof(*block),n*2));
                for(size_t j=0;j<n;++j){auto type=entries[j]>>12;auto rva=block->VirtualAddress+(entries[j]&4095);
                    if(type==IMAGE_REL_BASED_DIR64)*reinterpret_cast<Address*>(at(rva,8))+=delta;else require(type==IMAGE_REL_BASED_ABSOLUTE,"Nieobslugiwany rodzaj relokacji.");}
                offset+=block->SizeOfBlock;
            }
        }
        auto imports=opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        for(size_t offset=0;imports.Size&&offset+sizeof(IMAGE_IMPORT_DESCRIPTOR)<=imports.Size;offset+=sizeof(IMAGE_IMPORT_DESCRIPTOR)){
            auto desc=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(at(imports.VirtualAddress+offset,sizeof(IMAGE_IMPORT_DESCRIPTOR)));if(!desc->Name)break;
            const char* dll=str(desc->Name);HMODULE local=LoadLibraryA(dll);require(local!=nullptr,"Brak biblioteki zaleznosci DLL.");
            // Resolve API-set forwarders through their owning system DLL. Load missing
            // owners only by their verified absolute System32 path.
            try{for(size_t i=0;;++i){auto lookup=reinterpret_cast<IMAGE_THUNK_DATA64*>(at((desc->OriginalFirstThunk?desc->OriginalFirstThunk:desc->FirstThunk)+i*8,8));if(!lookup->u1.AddressOfData)break;
                    auto symbol=IMAGE_SNAP_BY_ORDINAL64(lookup->u1.Ordinal)?reinterpret_cast<const char*>(IMAGE_ORDINAL64(lookup->u1.Ordinal)):str(static_cast<size_t>(lookup->u1.AddressOfData)+2);
                    auto address=remoteFunction(pid,GetProcAddress(local,symbol),process);*reinterpret_cast<Address*>(at(desc->FirstThunk+i*8,8))=address;}
            }catch(...){FreeLibrary(local);throw;}FreeLibrary(local);
        }
        write(process,base,image.data(),image.size());DWORD old=0;
        require(VirtualProtectEx(process,reinterpret_cast<void*>(base),opt.SizeOfHeaders,PAGE_READONLY,&old)!=0,"Ochrona naglowka nie powiodla sie.");
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=section[i];size_t size=std::max(s.SizeOfRawData,s.Misc.VirtualSize);if(!size)continue;
            bool execute=(s.Characteristics&IMAGE_SCN_MEM_EXECUTE)!=0,read=(s.Characteristics&IMAGE_SCN_MEM_READ)!=0,writeable=(s.Characteristics&IMAGE_SCN_MEM_WRITE)!=0;
            DWORD protect=execute?(writeable?PAGE_EXECUTE_READWRITE:read?PAGE_EXECUTE_READ:PAGE_EXECUTE):(writeable?PAGE_READWRITE:read?PAGE_READONLY:PAGE_NOACCESS);
            require(VirtualProtectEx(process,reinterpret_cast<void*>(base+s.VirtualAddress),size,protect,&old)!=0,"Ochrona sekcji nie powiodla sie.");
        }
        FlushInstructionCache(process,reinterpret_cast<void*>(base),image.size());
        auto exception=opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        attached=true; // Registration calls may time out after having succeeded remotely.
        if(exception.Size){at(exception.VirtualAddress,exception.Size);
            auto fn=remoteFunction(pid,GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"RtlAddFunctionTable"));
            require(invoke(process,fn,base+exception.VirtualAddress,exception.Size/sizeof(RUNTIME_FUNCTION),base)!=0,"Rejestracja unwind nie powiodla sie.");registered=true;
        }
        // After runtime callbacks start, retain the mapped allocation on failure.
        // Freeing a possibly executing DLL is unsafe; process exit reclaims it.
        attached=true;
        if(tlsDir.Size){auto tls=reinterpret_cast<IMAGE_TLS_DIRECTORY64*>(at(tlsDir.VirtualAddress,sizeof(IMAGE_TLS_DIRECTORY64)));
            if(tls->AddressOfCallBacks){size_t rva=static_cast<size_t>(tls->AddressOfCallBacks-base);
                for(size_t i=0;i<64;++i){auto fn=*reinterpret_cast<Address*>(at(rva+i*8,8));if(!fn)break;require(fn>=base&&fn<base+image.size(),"Callback TLS poza DLL.");invoke(process,fn,base,DLL_PROCESS_ATTACH,0);}}
        }
        require(opt.AddressOfEntryPoint<image.size(),"Entry point poza DLL.");
        if(opt.AddressOfEntryPoint)require(invoke(process,base+opt.AddressOfEntryPoint,base,DLL_PROCESS_ATTACH,0)!=0,"Inicjalizacja CRT/DllMain nie powiodla sie.");
        MapBootstrap data{};data.thread=threadId;data.testOnly=testOnly?1:0;
        auto directory=std::filesystem::path(filename).parent_path().wstring()+L"\\";
        require(WideCharToMultiByte(CP_UTF8,0,directory.c_str(),-1,data.directory,sizeof(data.directory),nullptr,nullptr)>0,"Zbyt dluga sciezka DLL.");
        bootstrapData=reinterpret_cast<Address>(VirtualAllocEx(process,nullptr,sizeof(data),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));require(bootstrapData!=0,"Brak pamieci bootstrap.");
        write(process,bootstrapData,&data,sizeof(data));auto result=invoke(process,base+bootstrap,bootstrapData);
        require(result==0x53434152,"Bootstrap runtime nie powiodl sie.");
        // Keep the remote image in place, but clear the PE header after CRT,
        // imports, TLS and unwind registration have completed.
        std::vector<unsigned char> zero(opt.SizeOfHeaders);
        require(VirtualProtectEx(process,reinterpret_cast<void*>(base),zero.size(),PAGE_READWRITE,&old)!=0,"Nie mozna odblokowac naglowka PE.");
        write(process,base,zero.data(),zero.size());
        require(VirtualProtectEx(process,reinterpret_cast<void*>(base),zero.size(),PAGE_NOACCESS,&old)!=0,"Nie mozna zabezpieczyc wyczyszczonego naglowka PE.");
        if(testOnly)require(invoke(process,base+bootstrap,bootstrapData)==0x53434152,"Runtime nie dziala po usunieciu naglowka PE.");
        VirtualFreeEx(process,reinterpret_cast<void*>(bootstrapData),0,MEM_RELEASE);bootstrapData=0;
        return base;
    }catch(...){
        // Do not free bootstrap input after an uncertain callback timeout.
        if(!attached){if(registered){auto ex=opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];try{invoke(process,remoteFunction(pid,GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"RtlDeleteFunctionTable")),base+ex.VirtualAddress);}catch(...){throw;}}
            VirtualFreeEx(process,reinterpret_cast<void*>(base),0,MEM_RELEASE);}
        throw;
    }
}
}
