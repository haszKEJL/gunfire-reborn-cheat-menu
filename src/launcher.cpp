#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <new>
#include "overlay.hpp"
#include "manual_map.hpp"
DWORD target=0,windowThread=0;HWND gameWindow=nullptr;
BOOL CALLBACK windowProc(HWND w,LPARAM){DWORD id;auto thread=GetWindowThreadProcessId(w,&id);
    if(id==target&&IsWindowVisible(w)&&!GetWindow(w,GW_OWNER)){windowThread=thread;gameWindow=w;return FALSE;}return TRUE;}
int main(int argc,char** argv){
    auto has=[&](const char* arg){for(int i=1;i<argc;++i)if(std::string(argv[i])==arg)return true;return false;};
    bool preview=false;for(int i=1;i<argc;++i)if(std::string(argv[i]).rfind("--preview",0)==0)preview=true;
    bool probe=has("--probe"),inspect=has("--inspect"),renderTest=has("--render-test"),silentTest=has("--silent-test"),combatTest=has("--combat-test"),worldTest=has("--world-test");
    wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);std::wstring dir(path);dir=dir.substr(0,dir.find_last_of(L"\\/")+1);
    if(has("--silent-monitor")){
        DWORD pid=0;auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W item{};item.dwSize=sizeof(item);
        if(snapshot!=INVALID_HANDLE_VALUE){if(Process32FirstW(snapshot,&item))do{if(!_wcsicmp(item.szExeFile,L"Gunfire Reborn.exe")){pid=item.th32ProcessID;break;}}while(Process32NextW(snapshot,&item));CloseHandle(snapshot);}
        if(!pid){std::cerr<<"Gra nie dziala.\n";return 1;}
        auto map=OpenFileMappingW(FILE_MAP_READ,FALSE,sharedName(pid,L".map").c_str());
        auto mutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,sharedName(pid,L".mutex").c_str());
        auto data=map?static_cast<Shared*>(MapViewOfFile(map,FILE_MAP_READ,0,0,sizeof(Shared))):nullptr;
        if(!data||!mutex){std::cerr<<"Brak aktywnej sesji ScarletAim.\n";return 2;}
        for(int i=0;i<12;++i){if(WaitForSingleObject(mutex,100)==WAIT_OBJECT_0){const auto& f=data->frame;const auto& c=data->config;
            std::cout<<"t="<<i*2<<"s cfg="<<c.enabled<<"/"<<c.silent<<"/"<<c.menuOpen<<" fov="<<c.fov
                <<" targets="<<f.count<<" active="<<f.silentTargetAvailable<<" loop="<<f.loopTicks<<" age="<<(GetTickCount64()-f.time)<<"ms families="<<f.silentFamilies
                <<" calls(ray/cast/throw/parabola)="<<f.rayCalls<<"/"<<f.raycastCalls<<"/"<<f.throwCalls<<"/"<<f.parabolaCalls<<" redirects="<<f.silentRedirects
                <<" (cast="<<f.raycastRedirects<<",other="<<f.projectileRedirects<<",aimpos="<<f.aimPositionCalls<<"/"<<f.aimPositionRedirects<<") spread="<<f.spreadCalls<<"/"<<f.spreadLocalCalls<<" hits="<<f.hitInfoCalls<<" types=";
            for(int j=0;j<8;++j)std::cout<<f.targetTypes[j]<<(j==7?'\n':',');
            ReleaseMutex(mutex);}Sleep(2000);}
        UnmapViewOfFile(data);CloseHandle(map);CloseHandle(mutex);return 0;
    }
    if(has("--map-test")){try{auto base=manual_map::load(GetCurrentProcess(),GetCurrentProcessId(),GetCurrentThreadId(),dir+L"ScarletAim.dll",true);
        auto relocated=manual_map::load(GetCurrentProcess(),GetCurrentProcessId(),GetCurrentThreadId(),dir+L"ScarletAim.dll",true);
        manual_map::require(base!=relocated,"Test nie wymusil relokacji.");
        MEMORY_BASIC_INFORMATION info{};manual_map::require(VirtualQuery(reinterpret_cast<void*>(base),&info,sizeof(info))!=0&&info.Protect==PAGE_NOACCESS,"Naglowek PE nadal jest dostepny.");
        manual_map::require(manual_map::moduleBase(GetCurrentProcessId(),L"ScarletAim.dll")==0,"Manual map pojawil sie na liscie loadera.");
        std::cout<<"Manual mapping: PE/imports/forced relocation/CRT/unwind/bootstrap PASS, bases="<<std::hex<<base<<","<<relocated<<"\n";return 0;
    }catch(const std::exception& ex){std::cerr<<"Manual mapping: FAIL: "<<ex.what()<<"\n";return 6;}}
    if(preview)return runOverlay(nullptr,0,nullptr,nullptr,nullptr,dir+L"settings.bin",true);
    HANDLE singleton=CreateMutexW(nullptr,FALSE,L"Local\\GunfireReborn.ScarletAim.v6.singleton");
    if(!singleton||GetLastError()==ERROR_ALREADY_EXISTS){if(singleton)CloseHandle(singleton);std::cerr<<"ScarletAim jest juz uruchomiony. End zamyka poprzednia instancje.\n";return 3;}
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(snap!=INVALID_HANDLE_VALUE){if(Process32FirstW(snap,&entry))do{if(!_wcsicmp(entry.szExeFile,L"Gunfire Reborn.exe")){target=entry.th32ProcessID;break;}}while(Process32NextW(snap,&entry));CloseHandle(snap);}
    EnumWindows(windowProc,0);if(!windowThread){std::cerr<<"Uruchom Gunfire Reborn.\n";CloseHandle(singleton);return 1;}
    HANDLE map=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),sharedName(target,L".map").c_str());
    auto shared=map?static_cast<Shared*>(MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared))):nullptr;
    HANDLE mutex=CreateMutexW(nullptr,FALSE,sharedName(target,L".mutex").c_str());
    if(!shared||!mutex){std::cerr<<"Blad pamieci wspoldzielonej.\n";return 2;}
    new(shared) Shared{};
    HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_READ|PROCESS_VM_WRITE,FALSE,target);
    if(!process){std::cerr<<"Brak dostepu do procesu: "<<GetLastError()<<"\n";return 2;}
    try{manual_map::load(process,target,windowThread,dir+L"ScarletAim.dll");std::cout<<"Manual mapping: runtime zaladowany.\n";}
    catch(const std::exception& ex){std::cerr<<"Manual mapping: "<<ex.what()<<"\n";return 6;}
    auto command=[&](unsigned code){if(WaitForSingleObject(mutex,100)==WAIT_OBJECT_0){shared->command=code;++shared->commandSerial;ReleaseMutex(mutex);}};
    int result=0;
    if(silentTest||combatTest){
        if(WaitForSingleObject(mutex,100)==WAIT_OBJECT_0){auto& c=shared->config;c.enabled=true;c.onKey=false;c.keyMode=2;c.silent=true;c.noSpread=true;c.weaknessHit=combatTest;c.menuOpen=false;c.visibleOnly=true;c.scopedOnly=false;c.fov=179;c.maxDistance=500;c.esp=false;c.glow=false;ReleaseMutex(mutex);}
        SetForegroundWindow(gameWindow);
        std::cout<<(combatTest?"Combat diagnostic: Weakness Hit przez 20 sekund.\n":"Silent/No Spread diagnostic: strzelaj przez 30 sekund; test nie steruje mysza.\n");
        for(int i=0;i<(combatTest?200:300);++i){Sleep(100);
            if(i%50==49&&WaitForSingleObject(mutex,100)==WAIT_OBJECT_0){auto& f=shared->frame;
                std::cout<<"t="<<(i+1)/10<<"s targets="<<f.count<<" active="<<f.silentTargetAvailable<<" loop="<<f.loopTicks<<" age="<<(GetTickCount64()-f.time)<<"ms families="<<f.silentFamilies
                    <<" calls(ray/cast/throw/parabola)="<<f.rayCalls<<"/"<<f.raycastCalls<<"/"<<f.throwCalls<<"/"<<f.parabolaCalls<<" redirects="<<f.silentRedirects
                    <<" (cast="<<f.raycastRedirects<<",other="<<f.projectileRedirects<<",aimpos="<<f.aimPositionCalls<<"/"<<f.aimPositionRedirects<<") spread="<<f.spreadCalls<<"/"<<f.spreadLocalCalls<<" straight="<<f.straightShots<<" hits="<<f.hitInfoCalls<<" weak="<<f.weaknessRedirects<<" types=";
                for(int j=0;j<8;++j)std::cout<<f.targetTypes[j]<<(j==7?'\n':',');
                ReleaseMutex(mutex);}}
    }else if(worldTest){
        if(WaitForSingleObject(mutex,100)==WAIT_OBJECT_0){auto& c=shared->config;c.menuOpen=false;c.enabled=false;c.itemEsp=true;c.autoPickup=true;c.esp=false;c.glow=false;ReleaseMutex(mutex);}
        SetForegroundWindow(gameWindow);
        Sleep(4000);
        if(WaitForSingleObject(mutex,100)==WAIT_OBJECT_0){auto& f=shared->frame;
            std::cout<<"World: "<<f.status<<" | objects="<<f.itemCount<<" | drops="<<f.nearbyDrops<<" | pickup moves="<<f.pickupMoves<<"\n";
            result=f.pickupMoves>0?0:5;ReleaseMutex(mutex);}
    }else if(renderTest){
        shared->config.glow=true;shared->config.esp=false;shared->config.silent=true;shared->config.menuOpen=true;shared->config.enabled=false;
        for(int i=0;i<180;++i){if(i==130)command(5);Sleep(16);}
        if(WaitForSingleObject(mutex,100)==WAIT_OBJECT_0){auto& f=shared->frame;
            std::cout<<f.status<<" | renderers="<<f.rendererCount<<" | precise="<<f.preciseCount<<" | glow draws="<<f.glowDrawCount<<" | silent="<<f.silentReady<<" | common ray="<<f.silentRayReady<<"\n";
            result=f.ready&&f.rendererCount>0&&f.glowDrawCount>0&&f.silentReady&&f.silentRayReady?0:5;ReleaseMutex(mutex);
        }
        for(bool glow:{false,true,false}){
            if(WaitForSingleObject(mutex,100)==WAIT_OBJECT_0){shared->config.glow=glow;shared->config.glowColor[0]=0;shared->config.glowColor[1]=.85f;shared->config.glowColor[2]=1;ReleaseMutex(mutex);}
            for(int i=0;i<40;++i)Sleep(16);
            if(WaitForSingleObject(mutex,100)==WAIT_OBJECT_0){bool ok=glow?shared->frame.glowDrawCount>0:shared->frame.glowDrawCount==0;std::cout<<"Glow "<<(glow?"ON":"OFF")<<": "<<(ok?"PASS":"FAIL")<<"\n";if(!ok)result=5;ReleaseMutex(mutex);}
            if(glow){command(7);Sleep(400);}
        }
        command(6);Sleep(800);
    }else if(probe||inspect){command(inspect?3:1);Sleep(inspect?10000:2000);
        if(probe){if(WaitForSingleObject(mutex,100)==WAIT_OBJECT_0){std::cout<<shared->frame.status<<"\n";result=shared->frame.ready?0:4;ReleaseMutex(mutex);}else result=4;}
        std::cout<<"Szczegoly: bin/aim.log\n";
    }else {std::cout<<"Insert: menu | End: zamknij\n";result=runOverlay(gameWindow,windowThread,shared,mutex,process,dir+L"settings.bin",false,true);}
    command(4);
    bool stopped=false;
    for(int i=0;i<100;++i){
        if(process&&WaitForSingleObject(process,0)==WAIT_OBJECT_0){stopped=true;break;}
        if(WaitForSingleObject(mutex,0)==WAIT_OBJECT_0){stopped=shared->frame.shutdown;ReleaseMutex(mutex);}
        if(stopped)break;Sleep(10);
    }
    if(!stopped)std::cerr<<"Nie potwierdzono zamkniecia runtime; zajrzyj do aim.log.\n";
    if(process)CloseHandle(process);
    UnmapViewOfFile(shared);CloseHandle(map);CloseHandle(mutex);CloseHandle(singleton);return result;
}
