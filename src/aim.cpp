#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <cstring>
#include <string>
#include <fstream>
#include <algorithm>
#include <vector>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <tuple>
#include <unordered_map>
#include <set>
#include "shared.hpp"
#include "../vendor/minhook/include/MinHook.h"

namespace {
struct Vec { float x,y,z; };
struct Ray { Vec origin,direction; };
HMODULE module;
HHOOK mappedHook=nullptr;
HANDLE mappedStop=nullptr,mappedReady=nullptr;
HANDLE mappedLoopReady=nullptr;
bool mappedMode=false,loopInstalled=false;
void* loopAddress=nullptr;
using TimeFn=float(*)(void*);
TimeFn originalTime=nullptr;
DWORD mappedGameThread=0;
std::atomic_flag pumping=ATOMIC_FLAG_INIT;
unsigned pumpTicks=0;
std::string directory;
bool initialized=false, ready=false;
HANDLE mapping=nullptr,sharedMutex=nullptr;
Shared* shared=nullptr;
Config cfg{};
Frame frame{};
std::uint64_t lockedId=0;
bool wasKey=false,toggled=false;
bool stopping=false;
UINT message;
void log(const std::string& s) { std::ofstream(directory+"aim.log",std::ios::app)<<s<<'\n'; }
// Opaque IL2CPP handles: no game addresses or object layouts are hardcoded.
void* (*domain_get)();
const void** (*domain_get_assemblies)(void*,size_t*);
void* (*assembly_get_image)(const void*);
const char* (*image_get_name)(void*);
void* (*class_from_name)(void*,const char*,const char*);
void* (*class_get_method_from_name)(void*,const char*,int);
void* (*runtime_invoke)(void*,void*,void**,void**);
void* (*object_unbox)(void*);
void* (*object_get_class)(void*);
void* (*class_get_fields)(void*,void**);
void* (*class_get_parent)(void*);
const char* (*field_get_name)(void*);
void (*field_get_value)(void*,void*,void*);
void* (*class_get_methods)(void*,void**);
const char* (*method_get_name)(void*);
unsigned (*method_get_param_count)(void*);
void* (*method_get_param)(void*,unsigned);
char* (*type_get_name)(void*);
void (*il_free)(void*);
int (*class_value_size)(void*,unsigned*);
void* (*class_get_field_from_name)(void*,const char*);
void* (*field_get_type)(void*);
void (*field_static_get_value)(void*,void*);
void* (*class_get_type)(void*);
void* (*type_get_object)(void*);
void* (*object_new)(void*);
void* (*string_new)(const char*);
const wchar_t* (*string_chars)(void*);
int (*string_length)(void*);
std::uint32_t (*gchandle_new)(void*,bool);
void (*gchandle_free)(std::uint32_t);
void* (*array_new)(void*,uintptr_t);
uintptr_t (*array_length)(void*);
std::uint32_t (*object_get_size)(void*);
void* (*value_box)(void*,void*);
size_t (*image_get_class_count)(void*);
void* (*image_get_class)(void*,size_t);
const char* (*class_get_name)(void*);
const char* (*class_get_namespace)(void*);
const char* (*method_get_param_name)(void*,unsigned);
void* (*method_get_return_type)(void*);
unsigned (*method_get_flags)(void*,unsigned*);
void dumpRelevant() {
    auto domain=domain_get(); size_t n=0; auto assemblies=domain_get_assemblies(domain,&n);
    for(size_t i=0;i<n;++i) {
        auto im=assembly_get_image(assemblies[i]); auto imName=image_get_name(im);
        if(!imName || (std::strstr(imName,"UnityEngine")==nullptr && std::strcmp(imName,"Assembly-CSharp.dll")))continue;
        for(size_t j=0;j<image_get_class_count(im);++j) {
            auto c=image_get_class(im,j); const char* name=class_get_name(c);
            if(!name)continue;
            bool relevant=std::strstr(name,"Highlight")||std::strstr(name,"Outline")||std::strstr(name,"Glow")
                ||!std::strcmp(name,"RayCastCartoon")||!std::strcmp(name,"CameraCtrl")||!std::strcmp(name,"OCBodyPart")
                ||!std::strcmp(name,"HeroAttackCtrl")||!std::strcmp(name,"NewPlayerManager")||!std::strcmp(name,"NewObjectCache")||!std::strcmp(name,"NewPlayerObject")
                ||!std::strcmp(class_get_namespace(c),"SkillBolt")||std::strstr(name,"PlayerProp")||std::strstr(name,"Weapon")
                ||std::strstr(name,"SightLogic")||std::strstr(name,"Language")||std::strstr(name,"Localization")
                ||std::strstr(name,"DropOP")||std::strstr(name,"Chest")||std::strstr(name,"Merchant")
                ||!std::strcmp(class_get_namespace(c),"OCDrop")||!std::strcmp(class_get_namespace(c),"OCNpc")
                ||std::strstr(name,"HeadBar")||std::strstr(name,"BloodBar")||std::strstr(name,"MonsterData")
                ||std::strstr(name,"MonsterConfig")||std::strstr(name,"MonsterInfo");
            if(!relevant)continue;
            log(std::string("CLASS ")+class_get_namespace(c)+"."+name);
            void* it=nullptr;
            while(auto m=class_get_methods(c,&it)) {
                std::string s=std::string(" M ")+method_get_name(m);
                for(unsigned k=0;k<method_get_param_count(m);++k) {
                    auto type=type_get_name(method_get_param(m,k));s+=" | ";s+=type?type:"?";s+=" ";s+=method_get_param_name(m,k);
                    if(type)il_free(type);
                }
                log(s);
            }
            it=nullptr;while(auto f=class_get_fields(c,&it))log(std::string(" F ")+field_get_name(f));
        }
    }
}
void* monstersMethod; void* weakMethod; void* cameraMethod;void* monsterNameMethod;
void* mainCtrlField=nullptr,*isMonsterMethod=nullptr;
std::atomic<int> localPlayerId{0};
void* positionMethod; void* projectMethod; void* linecastMethod;
void* widthMethod; void* heightMethod;
void* hitTransformMethod; void* rootMethod; int hitSize=0;
void* centerMethod; void* cameraFovMethod; void* scopedField; void* ironField;
void* componentsMethod; void* boundsMethod; void* transformGameObject;
void* nameMethod; void* rendererType; void* objectDestroy;
void* materialClass;void* materialCtor;void* materialColor;void* materialInt;
void* glowMaterial=nullptr;void* glowCamera=nullptr;
void* cachedGlowShader=nullptr;
void* shaderBundle=nullptr;
std::vector<std::uint32_t> roots;
std::vector<std::uint32_t> glowRoots;
bool glowAvailable=false;
void* silentCtrlAddress=nullptr;void* silentEnableAddress=nullptr;
bool silentInstalled=false;
bool silentAttempted=false;
void* cameraRecoilAddress=nullptr;void* weaponRecoilAddress=nullptr;
using RecoilFn=void(*)(void*,void*);
RecoilFn originalCameraRecoil=nullptr,originalWeaponRecoil=nullptr;
bool combatHooksAttempted=false;
std::atomic<bool> noRecoilActive{false};
std::atomic<float> movementMultiplier{1},reloadMultiplier{1},fireMultiplier{1};
using PropertyIntFn=int(*)(void*,void*);
using SkillFloatFn=float(*)(void*,void*);
PropertyIntFn originalPropertyId=nullptr,originalMoveSpeed=nullptr,originalFireSpeed=nullptr;
SkillFloatFn originalReloadSpeed=nullptr;
using SkillIntFn=int(*)(void*,void*);
SkillIntFn originalReloadSpeedInt=nullptr;
void* moveSpeedAddress=nullptr,*fireSpeedAddress=nullptr,*reloadSpeedAddress=nullptr;
std::atomic<bool> noSpreadActive{false},weaknessActive{false};
using SpreadFn=float(*)(void*,void*);
SpreadFn originalServerSpread=nullptr,originalBaseSpread=nullptr;
void* serverSpreadAddress=nullptr,*baseSpreadAddress=nullptr;
using HitCtorFn=void(*)(void*,int,void*,void*,bool,Vec*,Vec*,void*,Vec*,void*,void*,void*);
HitCtorFn originalHitCtor=nullptr;
void* hitCtorAddress=nullptr,*weaknessString=nullptr,*isMainCtrlMethod=nullptr;
void* silentRayAddress=nullptr;
void* watchRayAddress=nullptr;
using RayFn=Ray(*)(void*);
RayFn originalRay=nullptr;
RayFn originalWatchRay=nullptr;
std::atomic<unsigned> silentRedirects{0};
std::atomic<unsigned> rayCalls{0},raycastCalls{0},throwCalls{0},parabolaCalls{0};
std::atomic<unsigned> raycastRedirects{0},projectileRedirects{0};
std::atomic<unsigned> aimPositionCalls{0},aimPositionRedirects{0};
std::atomic<unsigned> spreadCalls{0},spreadLocalCalls{0},hitInfoCalls{0};
std::atomic<unsigned> straightShots{0};
std::atomic<unsigned> weaknessRedirects{0};
using AimPositionFn=Vec(*)(void*,void*,bool,void*);
AimPositionFn originalServerAimPosition=nullptr,originalBaseAimPosition=nullptr;
void* serverAimPositionAddress=nullptr,*baseAimPositionAddress=nullptr;
using SkillEndFn=Vec(*)(void*,void*);
using CameraPositionFn=Vec(*)(void*,void*,void*);
SkillEndFn originalSkillEnd=nullptr;
CameraPositionFn originalCameraPosition=nullptr;
void* skillEndAddress=nullptr,*cameraPositionAddress=nullptr;
std::atomic<unsigned> targetTypes[8]{};
SRWLOCK silentLock=SRWLOCK_INIT;
Vec silentPosition{};ULONGLONG silentTime=0;
std::atomic<int> silentHoldKey{0};
using CtrlFn=void(*)(void*,void*,Vec*,Vec*,Vec*,int,float,float,int,void*,float,void*,float,float,float,int,int,void*);
using EnableFn=void(*)(void*,void*,Vec*,Vec*,Vec*,int,float,float,int,void*,float,void*,float,float,float,void*);
CtrlFn originalCtrl=nullptr;EnableFn originalEnable=nullptr;
template<class T> bool bind(T& p,const char* name) {
    p=reinterpret_cast<T>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),name));
    if(!p) log(std::string("Missing export: ")+name);
    return p!=nullptr;
}
void* image(const char* name) {
    size_t n=0; auto assemblies=domain_get_assemblies(domain_get(),&n);
    for(size_t i=0;assemblies && i<n;++i) {
        auto im=assembly_get_image(assemblies[i]); const char* s=image_get_name(im);
        if(s && (std::string(s)==name || std::string(s)==std::string(name)+".dll")) return im;
    }
    return nullptr;
}
void* method(void* im,const char* ns,const char* cls,const char* name,int args) {
    auto c=im?class_from_name(im,ns,cls):nullptr;
    auto m=c?class_get_method_from_name(c,name,args):nullptr;
    if(!m) {
        log(std::string("Missing method: ")+cls+"."+name);
        if(c) { void* it=nullptr; while(auto candidate=class_get_methods(c,&it)) {
            auto s=method_get_name(candidate);
            if(std::string(cls)!="Physics" || (s && std::strstr(s,"cast")))
            {
                std::string details=std::string("  candidate: ")+(s?s:"?");
                for(unsigned j=0;j<method_get_param_count(candidate);++j) {
                    auto type=type_get_name(method_get_param(candidate,j));
                    details+=" "; details+=type?type:"?"; if(type)il_free(type);
                }
                log(details);
            }
        } }
    }
    return m;
}
void* typedMethod(void* cls,const char* name,std::initializer_list<const char*> parameters) {
    for(auto c=cls;c;c=class_get_parent(c)) {
        void* it=nullptr;
        while(auto m=class_get_methods(c,&it)){
            if(std::strcmp(method_get_name(m),name)||method_get_param_count(m)!=parameters.size())continue;
            bool matches=true;unsigned i=0;
            for(auto expected:parameters){auto t=type_get_name(method_get_param(m,i++));bool ok=t&&!std::strcmp(t,expected);if(t)il_free(t);matches&=ok;}
            if(matches)return m;
        }
    }return nullptr;
}
void* call(void* m,void* obj=nullptr,void** args=nullptr) {
    if(!m) return nullptr;
    void* exception=nullptr; auto result=runtime_invoke(m,obj,args,&exception);
    if(exception){static std::set<void*> reported;if(reported.insert(m).second)log(std::string("Invocation failed: ")+method_get_name(m)+" / "+class_get_name(object_get_class(exception)));return nullptr;}
    return result;
}
template<class T> bool value(void* obj,T& out) {
    if(!obj) return false;
    auto p=object_unbox(obj); if(!p) return false;
    std::memcpy(&out,p,sizeof(T)); return true;
}
bool init() {
#define B(n) if(!bind(n,"il2cpp_" #n)) return false
    B(domain_get); B(domain_get_assemblies); B(assembly_get_image); B(image_get_name);
    B(class_from_name); B(class_get_method_from_name); B(runtime_invoke); B(object_unbox);
    B(object_get_class); B(class_get_fields); B(class_get_parent); B(field_get_name); B(field_get_value);
    B(class_get_methods); B(method_get_name); B(method_get_param_count);
    B(method_get_param); B(type_get_name);
    B(class_value_size);
    B(class_get_field_from_name); B(field_static_get_value); B(class_get_type); B(type_get_object);
    bind(field_get_type,"il2cpp_field_get_type");
    B(object_new); B(string_new); B(string_chars); B(string_length); B(gchandle_new); B(gchandle_free);
    B(array_new);
    bind(array_length,"il2cpp_array_length");bind(object_get_size,"il2cpp_object_get_size");bind(value_box,"il2cpp_value_box");
    B(image_get_class_count); B(image_get_class); B(class_get_name); B(class_get_namespace); B(method_get_param_name);
    B(method_get_return_type);
    bind(method_get_flags,"il2cpp_method_get_flags");
    if(!bind(il_free,"il2cpp_free")) return false;
#undef B
    if(!domain_get()) return false;
    auto game=image("Assembly-CSharp"),core=image("UnityEngine.CoreModule");
    if(field_get_type){auto c=class_from_name(game,"","NewPlayerObject");for(const char* key:{"DropOPCom","NPCOPCom","gameTrans","SID","Shape"}){
        auto f=c?class_get_field_from_name(c,key):nullptr;if(!f)continue;auto t=type_get_name(field_get_type(f));
        log(std::string("FIELD NewPlayerObject.")+key+" -> "+(t?t:"?"));if(t)il_free(t);
    }}
    for(const auto& spec:std::array<std::array<const char*,4>,14>{{
        {{"","CameraCtrl","Recoil","0"}},
        {{"","WeaponMotionCtrl","ApplyRecoil","0"}},
        {{"SkillBolt","CServerArg","GetSpreadAccuracy","1"}},
        {{"SkillBolt","CArgBase","GetSpreadAccuracy","1"}},
        {{"SkillBolt","CServerArg","CheckHitWeakness","1"}},
        {{"SkillBolt","CArgBase","CheckHitWeakness","1"}},
        {{"SkillBolt","CServerAction","WeaponDamage","4"}},
        {{"","NewPlayerManager","GetMonsterType","1"}},
        {{"SkillBolt","CArgBase","SkillGetTargetMonsterName","2"}},
        {{"SkillBolt","SkillFunction","GetCameraCenterRay","0"}},
        {{"SkillBolt","CArgBase","GetAimTargetPosition","3"}},
        {{"SkillBolt","CServerArg","GetAimTargetPosition","3"}},
        {{"SkillBolt","CArgBase","GetScreenRayEndPos","6"}},
        {{"","NewPlayerManager","IsMainCtrl","1"}}
    }}){
        auto c=class_from_name(game,spec[0],spec[1]);
        if(!c)continue;
        for(void* it=nullptr;;){auto m=class_get_methods(c,&it);if(!m)break;
            if(std::strcmp(method_get_name(m),spec[2])||method_get_param_count(m)!=static_cast<unsigned>(std::atoi(spec[3])))continue;
            auto ret=type_get_name(method_get_return_type(m));unsigned flags=method_get_flags?method_get_flags(m,nullptr):0;
            log(std::string("API ")+spec[1]+"."+spec[2]+" -> "+(ret?ret:"?")+" flags="+std::to_string(flags));if(ret)il_free(ret);
        }
    }
    monstersMethod=method(game,"","NewPlayerManager","GetMonsters",0);
    monsterNameMethod=method(game,"SkillBolt","CArgBase","SkillGetTargetMonsterName",2);
    auto managerClass=class_from_name(game,"","NewPlayerManager");
    mainCtrlField=managerClass?class_get_field_from_name(managerClass,"MainCtrl"):nullptr;
    isMonsterMethod=method(game,"","NewPlayerManager","IsMonster",1);
    weakMethod=method(game,"","OCBodyPart","GetWeakTrans",3);
    centerMethod=method(game,"","OCBodyPart","GetCenterTrans",0);
    cameraMethod=method(game,"","CameraManager","get_MainCameraCom",0);
    cameraFovMethod=method(core,"UnityEngine","Camera","get_fieldOfView",0);
    auto attack=class_from_name(game,"","HeroAttackCtrl");
    scopedField=attack?class_get_field_from_name(attack,"IsOnSnipe"):nullptr;
    ironField=attack?class_get_field_from_name(attack,"IsOnIronSight"):nullptr;
    transformGameObject=method(core,"UnityEngine","Component","get_gameObject",0);
    nameMethod=method(core,"UnityEngine","Object","get_name",0);
    componentsMethod=typedMethod(class_from_name(core,"UnityEngine","GameObject"),"GetComponentsInChildren",{"System.Type","System.Boolean"});
    boundsMethod=method(core,"UnityEngine","Renderer","get_bounds",0);
    auto rendererClass=class_from_name(core,"UnityEngine","Renderer");
    rendererType=rendererClass?type_get_object(class_get_type(rendererClass)):nullptr;
    if(rendererType)roots.push_back(gchandle_new(rendererType,false));
    materialClass=class_from_name(core,"UnityEngine","Material");
    // Select the Shader constructor explicitly (several one-argument overloads exist).
    void* ctorIt=nullptr;
    while(materialClass) {
        auto m=class_get_methods(materialClass,&ctorIt);if(!m)break;
        if(std::strcmp(method_get_name(m),".ctor")||method_get_param_count(m)!=1)continue;
        auto type=type_get_name(method_get_param(m,0));bool ok=type&&!std::strcmp(type,"UnityEngine.Shader");
        if(type)il_free(type);if(ok){materialCtor=m;break;}
    }
    auto findTyped=[&](const char* name,int argc,const char* first)->void* {
        void* it=nullptr;while(auto m=class_get_methods(materialClass,&it)) {
            if(std::strcmp(method_get_name(m),name)||static_cast<int>(method_get_param_count(m))!=argc)continue;
            auto t=type_get_name(method_get_param(m,0));bool ok=t&&!std::strcmp(t,first);if(t)il_free(t);if(ok)return m;
        }return nullptr;
    };
    materialColor=materialClass?findTyped("SetColor",2,"System.String"):nullptr;
    materialInt=materialClass?findTyped("SetInt",2,"System.String"):nullptr;
    objectDestroy=method(core,"UnityEngine","Object","Destroy",1);
    glowAvailable=materialCtor&&materialColor&&materialInt&&objectDestroy&&rendererType&&componentsMethod;
    positionMethod=method(core,"UnityEngine","Transform","get_position",0);
    rootMethod=method(core,"UnityEngine","Transform","get_root",0);
    projectMethod=method(core,"UnityEngine","Camera","WorldToScreenPoint",1);
    widthMethod=method(core,"UnityEngine","Screen","get_width",0);
    heightMethod=method(core,"UnityEngine","Screen","get_height",0);
    auto physics=image("UnityEngine.PhysicsModule");
    auto physicsClass=physics?class_from_name(physics,"UnityEngine","Physics"):nullptr;
    auto hitClass=physics?class_from_name(physics,"UnityEngine","RaycastHit"):nullptr;
    unsigned alignment=0;
    if(hitClass)hitSize=class_value_size(hitClass,&alignment);
    hitTransformMethod=method(physics,"UnityEngine","RaycastHit","get_transform",0);
    void* iter=nullptr;
    while(physicsClass) {
        auto m=class_get_methods(physicsClass,&iter); if(!m) break;
        if(std::strcmp(method_get_name(m),"Linecast") || method_get_param_count(m)!=5) continue;
        auto type=type_get_name(method_get_param(m,2));
        bool match=type && std::strcmp(type,"UnityEngine.RaycastHit&")==0;
        if(type)il_free(type);
        if(match) {linecastMethod=m;break;}
    }
    return monstersMethod && weakMethod && cameraMethod && positionMethod && projectMethod
        && widthMethod && heightMethod && linecastMethod && rootMethod && hitTransformMethod && hitSize>0 && hitSize<1024;
}
void* body(void* obj) {
    for(auto c=object_get_class(obj);c;c=class_get_parent(c)) {
        void* it=nullptr;
        while(auto f=class_get_fields(c,&it)) {
            auto name=field_get_name(f);
            if(name && (_stricmp(name,"BodyPartCom")==0 || _stricmp(name,"<BodyPartCom>k__BackingField")==0)) {
                void* result=nullptr; field_get_value(obj,f,&result); return result;
            }
        }
    }
    return nullptr;
}
bool foreground() {
    DWORD p=0; GetWindowThreadProcessId(GetForegroundWindow(),&p);
    return p==GetCurrentProcessId();
}
#include "runtime.inc"

}
extern "C" __declspec(dllexport) LRESULT CALLBACK AimHook(int code,WPARAM wp,LPARAM lp) {
    if(code>=0 && wp==PM_REMOVE) {
        auto msg=reinterpret_cast<MSG*>(lp);
        if(!message) message=RegisterWindowMessageW(kMessage);
        if(msg->message==message) {
            connectShared();
            if(!initialized) {
                initialized=true; if(directory.empty()){wchar_t path[32768]{}; GetModuleFileNameW(module,path,32768);
                int len=WideCharToMultiByte(CP_UTF8,0,path,-1,nullptr,0,nullptr,nullptr);
                std::string p(len,'\0'); WideCharToMultiByte(CP_UTF8,0,path,-1,p.data(),len,nullptr,nullptr);
                directory=p.substr(0,p.find_last_of("\\/")+1);
                }
                ready=init(); log(ready?"READY: IL2CPP methods resolved.":"FAILED: unsupported runtime; aiming disabled.");
            }
            if(ready&&mappedMode&&!loopInstalled)installGameLoop();
            if(ready&&msg->wParam==4)finish();
            else if(ready&&(msg->wParam==5||msg->wParam==6||msg->wParam==7))captureGame(static_cast<int>(msg->wParam));
            else if(ready&&msg->wParam==3)dumpRelevant();
            else if(ready)tick(msg->wParam==1);
            else {frame.ready=false;std::strcpy(frame.status,"Brak wymaganych metod IL2CPP - sprawdz aim.log.");publish();}
            msg->message=WM_NULL;
        }
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
DWORD WINAPI mappedWorker(void* parameter){
    auto thread=static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(parameter));
    mappedHook=SetWindowsHookExW(WH_GETMESSAGE,AimHook,nullptr,thread);
    SetEvent(mappedReady);
    if(mappedHook)PostThreadMessageW(thread,RegisterWindowMessageW(kMessage),1,0);
    if(mappedHook){WaitForSingleObject(mappedStop,INFINITE);CloseHandle(mappedStop);mappedStop=nullptr;}
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI MapStart(MapBootstrap* data){
    if(!data||!std::memchr(data->directory,0,sizeof(data->directory)))return 0;
    directory=data->directory;
    if(data->testOnly){
        // Exercise static C++ runtime initialization, allocation and x64 unwinding.
        try{throw std::runtime_error(std::string("mapping-probe"));}
        catch(const std::exception& ex){return std::strcmp(ex.what(),"mapping-probe")?0:0x53434152;}
    }
    HANDLE thread=OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,data->thread);
    if(!thread)return 0;auto owner=GetProcessIdOfThread(thread);CloseHandle(thread);
    if(owner!=GetCurrentProcessId())return 0;
    mappedMode=true;
    mappedGameThread=data->thread;
    mappedStop=CreateEventW(nullptr,TRUE,FALSE,nullptr);mappedReady=CreateEventW(nullptr,TRUE,FALSE,nullptr);mappedLoopReady=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!mappedStop||!mappedReady||!mappedLoopReady)return 0;
    auto worker=CreateThread(nullptr,0,mappedWorker,reinterpret_cast<void*>(static_cast<std::uintptr_t>(data->thread)),0,nullptr);
    if(!worker)return 0;CloseHandle(worker);
    auto status=WaitForSingleObject(mappedReady,5000);
    if(status==WAIT_OBJECT_0){CloseHandle(mappedReady);mappedReady=nullptr;if(!mappedHook){CloseHandle(mappedStop);mappedStop=nullptr;}}
    if(status!=WAIT_OBJECT_0||!mappedHook)return 0;
    status=WaitForSingleObject(mappedLoopReady,5000);
    if(status==WAIT_OBJECT_0){CloseHandle(mappedLoopReady);mappedLoopReady=nullptr;}
    else {
        if(loopAddress){MH_DisableHook(loopAddress);MH_RemoveHook(loopAddress);loopAddress=nullptr;loopInstalled=false;MH_Uninitialize();}
        if(mappedHook){UnhookWindowsHookEx(mappedHook);mappedHook=nullptr;SetEvent(mappedStop);}
    }
    return status==WAIT_OBJECT_0&&loopInstalled?0x53434152:0;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) { module=h; DisableThreadLibraryCalls(h); }
    return TRUE;
}
