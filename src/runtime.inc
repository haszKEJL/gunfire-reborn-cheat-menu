// Included inside the runtime namespace in aim.cpp.
struct WorldCandidate {void* transform;std::uint32_t root;int kind;const char* label;};
std::vector<WorldCandidate> worldCache;
void clearWorldCache(){for(auto& candidate:worldCache)gchandle_free(candidate.root);worldCache.clear();}
bool invokeOk(void* m,void* obj,void** args=nullptr) {
    if(!m)return false;
    void* exception=nullptr;runtime_invoke(m,obj,args,&exception);return !exception;
}
void* fieldObject(void* obj,const char* name) {
    for(auto c=object_get_class(obj);c;c=class_get_parent(c)) {
        auto f=class_get_field_from_name(c,name);if(f){void* v=nullptr;field_get_value(obj,f,&v);return v;}
    }return nullptr;
}
bool connectShared() {
    if(shared)return true;
    auto name=sharedName(GetCurrentProcessId(),L".map");
    mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name.c_str());
    if(!mapping)return false;
    shared=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));
    sharedMutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,sharedName(GetCurrentProcessId(),L".mutex").c_str());
    return shared&&sharedMutex&&shared->version==kVersion;
}
void publish() {
    frame.time=GetTickCount64();
    if(shared&&WaitForSingleObject(sharedMutex,0)==WAIT_OBJECT_0){shared->frame=frame;ReleaseMutex(sharedMutex);}
}
void clearSilent() {AcquireSRWLockExclusive(&silentLock);silentTime=0;ReleaseSRWLockExclusive(&silentLock);}
bool silentTarget(Vec& pos) {
    int key=silentHoldKey.load(std::memory_order_relaxed);if(key&&!(GetAsyncKeyState(key)&0x8000))return false;
    AcquireSRWLockShared(&silentLock);auto time=silentTime;pos=silentPosition;ReleaseSRWLockShared(&silentLock);
    return time && GetTickCount64()-time<100 && foreground();
}
void* bodyNode(void* part,const char* name){
    auto node=fieldObject(part,name);if(!node)return nullptr;
    auto cls=object_get_class(node);auto kind=class_get_name(cls);
    if(kind&&(!std::strcmp(kind,"Transform")||!std::strcmp(kind,"RectTransform")))return node;
    auto count=class_get_method_from_name(cls,"get_Count",0),item=class_get_method_from_name(cls,"get_Item",1);
    int n=0,index=0;if(!count||!item||!value(call(count,node),n)||n<1)return nullptr;
    void* args[]={&index};auto first=call(item,node,args);
    auto firstName=first?class_get_name(object_get_class(first)):nullptr;
    return firstName&&(!std::strcmp(firstName,"Transform")||!std::strcmp(firstName,"RectTransform"))?first:nullptr;
}
std::vector<void*> allPlayers(bool diagnostic){
    std::vector<void*> result;
    auto game=image("Assembly-CSharp"),mgr=class_from_name(game,"","NewPlayerManager");
    auto dictField=mgr?class_get_field_from_name(mgr,"PlayerDict"):nullptr;
    void* dict=nullptr;if(dictField)field_static_get_value(dictField,&dict);
    if(!dict)return result;
    auto dictClass=object_get_class(dict),getValues=class_get_method_from_name(dictClass,"get_Values",0);
    int count=0;if(!value(call(class_get_method_from_name(dictClass,"get_Count",0),dict),count)||count<0||count>4096)return result;
    auto values=call(getValues,dict);if(!values)return result;
    auto cls=class_from_name(game,"","NewPlayerObject");auto array=cls?array_new(cls,count):nullptr;
    if(!array)return result;
    auto copy=class_get_method_from_name(object_get_class(values),"CopyTo",2);
    int zero=0;void* args[]={array,&zero};bool copied=invokeOk(copy,values,args);
    if(diagnostic)log(std::string("PlayerDict count=")+std::to_string(count)+" CopyTo="+(copied?"YES":"NO"));
    if(!copied)return result;
    auto arrClass=object_get_class(array),get=typedMethod(arrClass,"GetValue",{"System.Int32"});
    if(!get)return result;
    for(int i=0;i<count;++i){void* idx[]={&i};auto object=call(get,array,idx);if(object)result.push_back(object);}
    return result;
}
bool collectionHasInt(void* collection,int id){
    if(!collection||id<=0)return false;
    auto cls=object_get_class(collection);
    for(const char* name:{"Contains","ContainsKey"}){
        auto m=typedMethod(cls,name,{"System.Int32"});
        if(!m)continue;
        bool found=false;void* args[]={&id};
        if(value(call(m,collection,args),found)&&found)return true;
    }
    return false;
}
Ray redirectCameraRay(Ray ray){
    ++rayCalls;
    Vec pos{};
    if(silentTarget(pos)){
        Vec delta{pos.x-ray.origin.x,pos.y-ray.origin.y,pos.z-ray.origin.z};
        float length=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
        if(std::isfinite(length)&&length>.01f){ray.direction={delta.x/length,delta.y/length,delta.z/length};++silentRedirects;}
    }
    return ray;
}
Ray hookCameraRay(void* methodInfo){return redirectCameraRay(originalRay(methodInfo));}
Ray hookWatchRay(void* methodInfo){return redirectCameraRay(originalWatchRay(methodInfo));}
bool localSkill(void* skill);
bool straightRayEnd(void* skill,const Vec* end,Vec& adjusted){
    if(!noSpreadActive.load(std::memory_order_relaxed)||!originalRay||!end||!localSkill(skill))return false;
    Ray ray=originalRay(nullptr);float length=std::hypot(end->x-ray.origin.x,end->y-ray.origin.y,end->z-ray.origin.z);
    float directionLength=std::hypot(ray.direction.x,ray.direction.y,ray.direction.z);
    if(!std::isfinite(length)||!std::isfinite(directionLength)||length<.01f||directionLength<.01f)return false;
    float scale=length/directionLength;
    adjusted={ray.origin.x+ray.direction.x*scale,ray.origin.y+ray.direction.y*scale,ray.origin.z+ray.direction.z*scale};
    ++straightShots;return true;
}
#include "projectiles.inc"
void hookCtrl(void* self,void* skill,Vec* start,Vec* show,Vec* end,int pierce,float distance,float speed,int target,
    void* effect,float life,void* trail,float trailLife,float radius,float fly,int pass,int check,void* methodInfo) {
    ++raycastCalls;
    Vec aim{};Vec adjusted{};Vec* final=end;
    if(target>=0&&target<8)++targetTypes[target];
    if(target==1&&end&&silentTarget(aim)){adjusted=aim;final=&adjusted;++raycastRedirects;}
    else if(target==1&&straightRayEnd(skill,end,adjusted))final=&adjusted;
    originalCtrl(self,skill,start,show,final,pierce,distance,speed,target,effect,life,trail,trailLife,radius,fly,pass,check,methodInfo);
}
void hookEnable(void* self,void* skill,Vec* start,Vec* checkStart,Vec* end,int pierce,float distance,float speed,int target,
    void* effect,float life,void* trail,float trailLife,float radius,float fly,void* methodInfo) {
    ++raycastCalls;
    Vec aim{};Vec adjusted{};Vec* final=end;
    if(target>=0&&target<8)++targetTypes[target];
    if(target==1&&end&&silentTarget(aim)){adjusted=aim;final=&adjusted;++raycastRedirects;}
    else if(target==1&&straightRayEnd(skill,end,adjusted))final=&adjusted;
    originalEnable(self,skill,start,checkStart,final,pierce,distance,speed,target,effect,life,trail,trailLife,radius,fly,methodInfo);
}
bool installSilent() {
    if(silentInstalled)return true;
    auto game=image("Assembly-CSharp");
    auto ctrl=method(game,"SkillBolt","RayCastCartoon","EnableCtrl",16);
    auto enable=method(game,"SkillBolt","RayCastCartoon","Enable",14);
    if(!ctrl||!enable)return false;
    const char* expected[]={"SkillBolt.CSkillBase","UnityEngine.Vector3","UnityEngine.Vector3","UnityEngine.Vector3",
        "System.Int32","System.Single","System.Single","GO_ENUM.OBJ_TYPE","UnityEngine.Object","System.Single",
        "UnityEngine.Object","System.Single","System.Single","System.Single","System.Int32","GO_ENUM.EXT_CHECK"};
    const std::array<std::pair<void*,unsigned>,2> methods{{{ctrl,16u},{enable,14u}}};
    for(const auto& pair:methods)for(unsigned i=0;i<pair.second;++i) {
        auto t=type_get_name(method_get_param(pair.first,i));bool ok=t&&!std::strcmp(t,expected[i]);if(t)il_free(t);
        if(!ok){log("Silent signature mismatch; disabled.");return false;}
    }
    // MethodInfo begins with its native entry pointer in this verified IL2CPP build.
    silentCtrlAddress=*static_cast<void**>(ctrl);silentEnableAddress=*static_cast<void**>(enable);
    auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(MH_CreateHook(silentCtrlAddress,reinterpret_cast<void*>(hookCtrl),reinterpret_cast<void**>(&originalCtrl))!=MH_OK)return false;
    if(MH_CreateHook(silentEnableAddress,reinterpret_cast<void*>(hookEnable),reinterpret_cast<void**>(&originalEnable))!=MH_OK){MH_RemoveHook(silentCtrlAddress);return false;}
    if(MH_EnableHook(silentCtrlAddress)!=MH_OK||MH_EnableHook(silentEnableAddress)!=MH_OK){
        MH_DisableHook(silentCtrlAddress);MH_DisableHook(silentEnableAddress);
        MH_RemoveHook(silentCtrlAddress);MH_RemoveHook(silentEnableAddress);return false;
    }
    for(auto spec:std::array<std::tuple<const char*,void*,RayFn*,void**>,2>{{
        {"GetCameraCenterRay",reinterpret_cast<void*>(hookCameraRay),&originalRay,&silentRayAddress},
        {"GetWatchCameraCenterRay",reinterpret_cast<void*>(hookWatchRay),&originalWatchRay,&watchRayAddress}}}){
        auto ray=method(game,"SkillBolt","SkillFunction",std::get<0>(spec),0);
        auto ret=ray?type_get_name(method_get_return_type(ray)):nullptr;
        bool ok=ret&&!std::strcmp(ret,"UnityEngine.Ray")&&method_get_flags&&(method_get_flags(ray,nullptr)&0x10);
        if(ret)il_free(ret);if(!ok)continue;
        auto address=*static_cast<void**>(ray);
        if(address==silentRayAddress||MH_CreateHook(address,std::get<1>(spec),reinterpret_cast<void**>(std::get<2>(spec)))!=MH_OK)continue;
        if(MH_EnableHook(address)==MH_OK)*std::get<3>(spec)=address;else MH_RemoveHook(address);
    }
    installProjectileHooks(game);
    silentInstalled=true;log(std::string("Silent hooks installed: RayCastCartoon; common camera ray=")+(silentRayAddress?"YES":"NO"));return true;
}
unsigned long long glowTick=0;
struct MaterialOverride{void* renderer;void* materials;std::uint32_t rendererRoot,materialsRoot;unsigned long long seen;};
std::vector<MaterialOverride> overrides;
void restoreMaterials(){
    for(auto& o:overrides){auto m=class_get_method_from_name(object_get_class(o.renderer),"set_sharedMaterials",1);void* args[]={o.materials};invokeOk(m,o.renderer,args);gchandle_free(o.rendererRoot);gchandle_free(o.materialsRoot);}
    overrides.clear();
}
void* loadedShader(const char* wanted){
    if(cachedGlowShader)return cachedGlowShader;
    if(!std::strcmp(wanted,"ScarletAim/ModelGlow")){
        auto assetImage=image("UnityEngine.AssetBundleModule");
        auto assetClass=assetImage?class_from_name(assetImage,"UnityEngine","AssetBundle"):nullptr;
        auto load=assetClass?typedMethod(assetClass,"LoadFromFile_Internal",{"System.String","System.UInt32","System.UInt64"}):nullptr;
        if(!load&&assetClass)load=typedMethod(assetClass,"LoadFromFile",{"System.String","System.UInt32","System.UInt64"});
        unsigned crc=0;std::uint64_t offset=0;void* args[]={string_new((directory+"glow.bundle").c_str()),&crc,&offset};
        shaderBundle=call(load,nullptr,args);
        if(shaderBundle){
            roots.push_back(gchandle_new(shaderBundle,false));
            auto type=type_get_object(class_get_type(class_from_name(image("UnityEngine.CoreModule"),"UnityEngine","Shader")));
            auto loadAll=typedMethod(assetClass,"LoadAllAssets",{"System.Type"});void* allArgs[]={type};auto shaders=call(loadAll,shaderBundle,allArgs);
            if(!shaders){auto internal=typedMethod(assetClass,"LoadAssetWithSubAssets_Internal",{"System.String","System.Type"});void* internalArgs[]={string_new(""),type};shaders=call(internal,shaderBundle,internalArgs);}
            if(shaders){auto cls=object_get_class(shaders);int n=0;value(call(class_get_method_from_name(cls,"get_Length",0),shaders),n);auto get=typedMethod(cls,"GetValue",{"System.Int32"});
                for(int i=0;i<n&&i<128;++i){void* idx[]={&i};auto sh=call(get,shaders,idx);auto nm=sh?call(nameMethod,sh):nullptr;if(!nm)continue;
                    std::wstring ws(string_chars(nm),string_length(nm));std::string name(ws.begin(),ws.end());log("Bundle shader: "+name);if(name==wanted){cachedGlowShader=sh;roots.push_back(gchandle_new(sh,false));return sh;}
                }
            }
        }
        log("Local glow bundle could not be loaded.");return nullptr;
    }
    return nullptr;
}

void removeGlow() {
    restoreMaterials();
    if(glowMaterial){void* args[]={glowMaterial};invokeOk(objectDestroy,nullptr,args);}
    glowMaterial=nullptr;glowCamera=nullptr;
    for(auto handle:glowRoots)gchandle_free(handle);glowRoots.clear();
}
bool prepareGlow(void* cam) {
    if(!cfg.glow){if(glowMaterial)removeGlow();return false;}
    if(!glowAvailable)return false;
    if(glowCamera!=cam&&glowMaterial)removeGlow();
    if(!glowMaterial){
        auto shader=loadedShader("ScarletAim/ModelGlow");
        if(!shader){log("Glow shader unavailable: bin/glow.bundle");glowAvailable=false;return false;}
        glowMaterial=object_new(materialClass);void* matArgs[]={shader};
        if(!glowMaterial||!invokeOk(materialCtor,glowMaterial,matArgs)){glowMaterial=nullptr;glowAvailable=false;return false;}
        auto keyword=typedMethod(materialClass,"EnableKeyword",{"System.String"});void* keyArgs[]={string_new("ENABLE_CONTOUR")};invokeOk(keyword,glowMaterial,keyArgs);
        auto setFloat=typedMethod(materialClass,"SetFloat",{"System.String","System.Single"});
        for(auto pair:std::array<std::pair<const char*,float>,3>{{{"_ShowContourTime",1e9f},{"_CustomTime",0.f},{"_ContourWidth",.02f}}}){float v=pair.second;void* args[]={string_new(pair.first),&v};invokeOk(setFloat,glowMaterial,args);}
        glowRoots.push_back(gchandle_new(glowMaterial,false));
        glowCamera=cam;
    }
    struct Color{float r,g,b,a;} color{cfg.glowColor[0]*cfg.glowStrength,cfg.glowColor[1]*cfg.glowStrength,cfg.glowColor[2]*cfg.glowStrength,cfg.glowColor[3]};
    void* colorArgs[]={string_new("_ContourColor"),&color};invokeOk(materialColor,glowMaterial,colorArgs);
    int depth=cfg.glowThroughWalls?8:4;void* depthArgs[]={string_new("_ZTest"),&depth};invokeOk(materialInt,glowMaterial,depthArgs);
    return true;
}
int applyGlow(void* renderer){
    for(auto& o:overrides)if(o.renderer==renderer){o.seen=glowTick;return 1;}
    auto cls=object_get_class(renderer);auto get=class_get_method_from_name(cls,"get_sharedMaterials",0);auto set=class_get_method_from_name(cls,"set_sharedMaterials",1);
    auto original=call(get,renderer);if(!original||!set)return 0;
    int n=0;value(call(class_get_method_from_name(object_get_class(original),"get_Length",0),original),n);if(n<1||n>32)return 0;
    auto replacement=array_new(materialClass,n);if(!replacement)return 0;
    auto setValue=typedMethod(object_get_class(replacement),"SetValue",{"System.Object","System.Int32"});
    for(int i=0;i<n;++i){void* args[]={glowMaterial,&i};if(!invokeOk(setValue,replacement,args))return 0;}
    void* args[]={replacement};if(!invokeOk(set,renderer,args))return 0;
    overrides.push_back({renderer,original,gchandle_new(renderer,false),gchandle_new(original,false),glowTick});return n;
}
std::vector<void*> renderers(void* root) {
    std::vector<void*> result;if(!componentsMethod||!rendererType||!root)return result;
    auto go=call(transformGameObject,root);if(!go)return result;
    bool inactive=false;void* args[]={rendererType,&inactive};auto array=call(componentsMethod,go,args);if(!array){static bool once=false;if(!once){log("Renderer array unavailable.");once=true;}return result;}
    // Use System.Array accessors rather than depending on native array layout.
    auto cls=object_get_class(array);auto lenMethod=class_get_method_from_name(cls,"get_Length",0);
    auto getMethod=typedMethod(cls,"GetValue",{"System.Int32"});
    int len=0;if(!getMethod||!value(call(lenMethod,array),len)||len<0||len>256){static bool once=false;if(!once){log("Renderer array access failed: len="+std::to_string(len)+"; get="+std::to_string(getMethod!=nullptr));once=true;}return result;}
    for(int i=0;i<len;++i){void* idx[]={&i};auto r=call(getMethod,array,idx);if(!r)continue;
        auto name=class_get_name(object_get_class(r));
        // Particles, trails and line renderers have effect bounds, not body bounds.
        if(name&&(!std::strcmp(name,"SkinnedMeshRenderer")||!std::strcmp(name,"MeshRenderer")))result.push_back(r);
    }
    return result;
}
bool project(void* cam,Vec p,Vec& screen) {
    void* args[]={&p};return value(call(projectMethod,cam,args),screen)&&std::isfinite(screen.x)&&std::isfinite(screen.y)&&screen.z>.01f;
}
bool visible(Vec origin,Vec pos,void* targetRoot) {
    Vec delta{pos.x-origin.x,pos.y-origin.y,pos.z-origin.z};
    float length=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
    if(length>.5f){float scale=.4f/length;pos={pos.x-delta.x*scale,pos.y-delta.y*scale,pos.z-delta.z*scale};}
    std::vector<unsigned char> hit(static_cast<size_t>(hitSize));int mask=~4,triggers=1;bool blocked=true;
    void* args[]={&origin,&pos,hit.data(),&mask,&triggers};if(!value(call(linecastMethod,nullptr,args),blocked))return false;
    if(!blocked)return true;
    auto trans=call(hitTransformMethod,hit.data());
    for(int depth=0;trans&&depth<64;++depth){if(trans==targetRoot)return true;trans=call(class_get_method_from_name(object_get_class(trans),"get_parent",0),trans);}
    return false;
}
void hookCameraRecoil(void* self,void* info){if(!noRecoilActive.load(std::memory_order_relaxed))originalCameraRecoil(self,info);}
void hookWeaponRecoil(void* self,void* info){if(!noRecoilActive.load(std::memory_order_relaxed))originalWeaponRecoil(self,info);}
bool localProperty(void* self){int id=localPlayerId.load(std::memory_order_relaxed);return id>0&&self&&originalPropertyId&&originalPropertyId(self,nullptr)==id;}
int hookMoveSpeed(void* self,void* info){
    int base=originalMoveSpeed(self,info);
    return localProperty(self)?static_cast<int>(std::clamp(base*movementMultiplier.load(std::memory_order_relaxed),0.f,100000.f)):base;
}
int hookFireSpeed(void* self,void* info){
    int base=originalFireSpeed(self,info);
    return localProperty(self)?static_cast<int>(std::clamp(base*fireMultiplier.load(std::memory_order_relaxed),0.f,100000.f)):base;
}
bool localSkill(void* skill){
    if(!skill)return false;
    auto f=class_get_field_from_name(object_get_class(skill),"AttID");if(!f)return false;
    int id=0;field_get_value(skill,f,&id);if(id<=0)return false;
    return id==localPlayerId.load(std::memory_order_relaxed);
}
float hookReloadSpeed(void* skill,void* info){
    float base=originalReloadSpeed(skill,info);
    if(!localSkill(skill)||!std::isfinite(base)||base<=0)return base;
    return base*reloadMultiplier.load(std::memory_order_relaxed);
}
int hookReloadSpeedInt(void* skill,void* info){
    int base=originalReloadSpeedInt(skill,info);
    return localSkill(skill)?static_cast<int>(std::clamp(base*reloadMultiplier.load(std::memory_order_relaxed),0.f,100000.f)):base;
}
void installSpeedHooks(void* game){
    auto prop=class_from_name(game,"","PlayerProp");
    auto id=prop?class_get_method_from_name(prop,"get_ID",0):nullptr;
    if(id){auto ret=type_get_name(method_get_return_type(id));
        if(ret&&!std::strcmp(ret,"System.Int32"))originalPropertyId=reinterpret_cast<PropertyIntFn>(*static_cast<void**>(id));
        if(ret)il_free(ret);}
    auto property=[&](const char* name,void* hook,PropertyIntFn& original,void*& address){
        auto m=prop?class_get_method_from_name(prop,name,0):nullptr;
        auto ret=m?type_get_name(method_get_return_type(m)):nullptr;
        bool ok=originalPropertyId&&ret&&!std::strcmp(ret,"System.Int32")&&method_get_flags&&!(method_get_flags(m,nullptr)&0x10);
        if(ret)il_free(ret);if(!ok)return;
        address=*static_cast<void**>(m);
        if(MH_CreateHook(address,hook,reinterpret_cast<void**>(&original))!=MH_OK||MH_EnableHook(address)!=MH_OK){MH_RemoveHook(address);address=nullptr;}
    };
    property("get_Speed",reinterpret_cast<void*>(hookMoveSpeed),originalMoveSpeed,moveSpeedAddress);
    property("get_AttSpeed",reinterpret_cast<void*>(hookFireSpeed),originalFireSpeed,fireSpeedAddress);
    auto arg=class_from_name(game,"SkillBolt","CArgBase");
    auto m=arg?typedMethod(arg,"GetReloadSpeed",{"SkillBolt.CSkillBase"}):nullptr;
    auto ret=m?type_get_name(method_get_return_type(m)):nullptr;
    bool ok=ret&&(!std::strcmp(ret,"System.Single")||!std::strcmp(ret,"System.Int32"))&&method_get_flags&&(method_get_flags(m,nullptr)&0x10);
    bool integer=ret&&!std::strcmp(ret,"System.Int32");
    if(ret)il_free(ret);
    if(ok){reloadSpeedAddress=*static_cast<void**>(m);
        auto hook=integer?reinterpret_cast<void*>(hookReloadSpeedInt):reinterpret_cast<void*>(hookReloadSpeed);
        auto original=integer?reinterpret_cast<void**>(&originalReloadSpeedInt):reinterpret_cast<void**>(&originalReloadSpeed);
        if(MH_CreateHook(reloadSpeedAddress,hook,original)!=MH_OK||MH_EnableHook(reloadSpeedAddress)!=MH_OK){MH_RemoveHook(reloadSpeedAddress);reloadSpeedAddress=nullptr;}}
    log("Speed hooks: movement="+std::to_string(moveSpeedAddress!=nullptr)+" fire="+std::to_string(fireSpeedAddress!=nullptr)+" reload="+std::to_string(reloadSpeedAddress!=nullptr));
}
Vec hookServerAimPosition(void* skill,void* cartoon,bool first,void* info){
    ++aimPositionCalls;auto original=originalServerAimPosition(skill,cartoon,first,info);Vec selected{};
    if(localSkill(skill)&&silentTarget(selected)){++aimPositionRedirects;return selected;}return original;
}
Vec hookBaseAimPosition(void* skill,void* cartoon,bool first,void* info){
    ++aimPositionCalls;auto original=originalBaseAimPosition(skill,cartoon,first,info);Vec selected{};
    if(localSkill(skill)&&silentTarget(selected)){++aimPositionRedirects;return selected;}return original;
}
Vec hookSkillEnd(void* skill,void* info){
    auto original=originalSkillEnd(skill,info);Vec selected{};
    if(localSkill(skill)&&silentTarget(selected)){++aimPositionRedirects;return selected;}return original;
}
Vec hookCameraPosition(void* skill,void* cartoon,void* info){
    auto original=originalCameraPosition(skill,cartoon,info);Vec selected{};
    if(localSkill(skill)&&silentTarget(selected)){++aimPositionRedirects;return selected;}return original;
}
float hookServerSpread(void* skill,void* info){
    ++spreadCalls;auto original=originalServerSpread(skill,info);bool local=localSkill(skill);
    if(local)++spreadLocalCalls;
    static std::atomic<int> samples{0};if(samples.fetch_add(1)<12)log("Server spread sample="+std::to_string(original)+" local="+std::to_string(local));
    return noSpreadActive.load(std::memory_order_relaxed)&&local?0.f:original;
}
float hookBaseSpread(void* skill,void* info){
    ++spreadCalls;auto original=originalBaseSpread(skill,info);bool local=localSkill(skill);
    if(local)++spreadLocalCalls;
    static std::atomic<int> samples{0};if(samples.fetch_add(1)<12)log("Base spread sample="+std::to_string(original)+" local="+std::to_string(local));
    return noSpreadActive.load(std::memory_order_relaxed)&&local?0.f:original;
}
void hookHitCtor(void* self,int victim,void* lua,void* tran,bool life,Vec* pos,Vec* normal,void* hit,Vec* forward,void* part,void* server,void* info){
    ++hitInfoCalls;
    bool monster=false;void* victimArgs[]={&victim};
    if(isMonsterMethod&&victim>0)value(call(isMonsterMethod,nullptr,victimArgs),monster);
    if(monster&&weaknessActive.load(std::memory_order_relaxed)&&weaknessString){part=weaknessString;++weaknessRedirects;}
    originalHitCtor(self,victim,lua,tran,life,pos,normal,hit,forward,part,server,info);
}
void installCombatHooks(){
    auto game=image("Assembly-CSharp");
    auto hookStatus=MH_Initialize();if(hookStatus!=MH_OK&&hookStatus!=MH_ERROR_ALREADY_INITIALIZED)return;
    installSpeedHooks(game);
    auto install=[&](const char* cls,const char* name,void* hook,RecoilFn& original,void*& address){
        auto m=method(game,"",cls,name,0);if(!m)return;
        auto ret=type_get_name(method_get_return_type(m));bool ok=ret&&!std::strcmp(ret,"System.Void");if(ret)il_free(ret);
        if(!ok)return;address=*static_cast<void**>(m);
        if(MH_CreateHook(address,hook,reinterpret_cast<void**>(&original))!=MH_OK||MH_EnableHook(address)!=MH_OK){MH_RemoveHook(address);address=nullptr;}
    };
    install("CameraCtrl","Recoil",reinterpret_cast<void*>(hookCameraRecoil),originalCameraRecoil,cameraRecoilAddress);
    install("WeaponMotionCtrl","ApplyRecoil",reinterpret_cast<void*>(hookWeaponRecoil),originalWeaponRecoil,weaponRecoilAddress);
    isMainCtrlMethod=method(game,"","NewPlayerManager","IsMainCtrl",1);
    for(auto spec:std::array<std::tuple<const char*,void*,SpreadFn*,void**>,2>{{
        {"CServerArg",reinterpret_cast<void*>(hookServerSpread),&originalServerSpread,&serverSpreadAddress},
        {"CArgBase",reinterpret_cast<void*>(hookBaseSpread),&originalBaseSpread,&baseSpreadAddress}}}){
        auto m=method(game,"SkillBolt",std::get<0>(spec),"GetSpreadAccuracy",1);
        auto ret=m?type_get_name(method_get_return_type(m)):nullptr;
        bool ok=ret&&!std::strcmp(ret,"System.Single")&&method_get_flags&&(method_get_flags(m,nullptr)&0x10);
        if(ret)il_free(ret);if(!ok)continue;auto address=*static_cast<void**>(m);
        if((address==serverSpreadAddress||address==baseSpreadAddress)||MH_CreateHook(address,std::get<1>(spec),reinterpret_cast<void**>(std::get<2>(spec)))!=MH_OK)continue;
        if(MH_EnableHook(address)==MH_OK)*std::get<3>(spec)=address;else MH_RemoveHook(address);
    }
    auto hitClass=class_from_name(game,"SkillBolt","SClientHitInfo");
    auto ctor=hitClass?typedMethod(hitClass,".ctor",{"System.Int32","LuaObjectBase","UnityEngine.Transform","System.Boolean","UnityEngine.Vector3","UnityEngine.Vector3","UnityEngine.Transform","UnityEngine.Vector3","System.String","System.String"}):nullptr;
    if(ctor){hitCtorAddress=*static_cast<void**>(ctor);
        if(MH_CreateHook(hitCtorAddress,reinterpret_cast<void*>(hookHitCtor),reinterpret_cast<void**>(&originalHitCtor))!=MH_OK||MH_EnableHook(hitCtorAddress)!=MH_OK){MH_RemoveHook(hitCtorAddress);hitCtorAddress=nullptr;}
        else {weaknessString=string_new("Monster_Weakness");if(weaknessString)roots.push_back(gchandle_new(weaknessString,false));}}
    for(auto spec:std::array<std::tuple<const char*,void*,AimPositionFn*,void**>,2>{{
        {"CServerArg",reinterpret_cast<void*>(hookServerAimPosition),&originalServerAimPosition,&serverAimPositionAddress},
        {"CArgBase",reinterpret_cast<void*>(hookBaseAimPosition),&originalBaseAimPosition,&baseAimPositionAddress}}}){
        auto m=method(game,"SkillBolt",std::get<0>(spec),"GetAimTargetPosition",3);
        auto ret=m?type_get_name(method_get_return_type(m)):nullptr;
        bool ok=ret&&!std::strcmp(ret,"UnityEngine.Vector3")&&method_get_flags&&(method_get_flags(m,nullptr)&0x10);
        if(ret)il_free(ret);if(!ok)continue;auto address=*static_cast<void**>(m);
        if(address==serverAimPositionAddress||address==baseAimPositionAddress||MH_CreateHook(address,std::get<1>(spec),reinterpret_cast<void**>(std::get<2>(spec)))!=MH_OK)continue;
        if(MH_EnableHook(address)==MH_OK)*std::get<3>(spec)=address;else MH_RemoveHook(address);
    }
    auto arg=class_from_name(game,"SkillBolt","CArgBase");
    auto installSkillEnd=[&](const char* name,std::initializer_list<const char*> signature,void* hook,void** original,void*& address){
        auto m=arg?typedMethod(arg,name,signature):nullptr;
        auto ret=m?type_get_name(method_get_return_type(m)):nullptr;
        bool ok=ret&&!std::strcmp(ret,"UnityEngine.Vector3")&&method_get_flags&&(method_get_flags(m,nullptr)&0x10);
        if(ret)il_free(ret);if(!ok)return;
        address=*static_cast<void**>(m);
        if(MH_CreateHook(address,hook,original)!=MH_OK||MH_EnableHook(address)!=MH_OK){MH_RemoveHook(address);address=nullptr;}
    };
    installSkillEnd("CrtArgSkillEndPos",{"SkillBolt.CSkillBase"},reinterpret_cast<void*>(hookSkillEnd),reinterpret_cast<void**>(&originalSkillEnd),skillEndAddress);
    installSkillEnd("GetCameraCenterPosition",{"SkillBolt.CSkillBase","SkillBolt.CCartoonBase"},reinterpret_cast<void*>(hookCameraPosition),reinterpret_cast<void**>(&originalCameraPosition),cameraPositionAddress);
    log(std::string("NoRecoil hooks: camera=")+(cameraRecoilAddress?"YES":"NO")+" weapon="+(weaponRecoilAddress?"YES":"NO"));
    log(std::string("Combat hooks: spread=")+(serverSpreadAddress||baseSpreadAddress?"YES":"NO")+" hit="+(hitCtorAddress?"YES":"NO"));
    log(std::string("Generic aim position hooks: server=")+(serverAimPositionAddress?"YES":"NO")+" base="+(baseAimPositionAddress?"YES":"NO"));
}
struct Matrix4{float m[16];};
struct BakedShape{void* renderer=nullptr;std::uint32_t root=0;ULONGLONG updated=0,seen=0;std::vector<Vec> vertices;};
std::vector<BakedShape> bakedShapes;
void* bakeMesh=nullptr;std::uint32_t bakeMeshRoot=0;
void clearBakedShapes(){
    for(auto& shape:bakedShapes)gchandle_free(shape.root);bakedShapes.clear();
    if(bakeMesh){void* args[]={bakeMesh};invokeOk(objectDestroy,nullptr,args);bakeMesh=nullptr;gchandle_free(bakeMeshRoot);bakeMeshRoot=0;}
}
Vec transformPoint(const Matrix4& matrix,Vec p){auto& m=matrix.m;return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};}
bool preciseBounds(void* renderer,const Matrix4& world,const Matrix4& view,const Matrix4& projection,Enemy& entry,int width,int height){
    if(!array_length||!object_get_size||!value_box)return false;
    static size_t header=0;static bool checked=false;
    if(!checked){checked=true;
        auto vc=class_from_name(image("UnityEngine.CoreModule"),"UnityEngine","Vector3");
        Vec sentinel[2]{{123.25f,-456.5f,789.75f},{-321.25f,654.5f,-987.75f}};
        auto arr=vc?array_new(vc,2):nullptr;
        if(arr){auto root=gchandle_new(arr,false);auto set=typedMethod(object_get_class(arr),"SetValue",{"System.Object","System.Int32"});bool ok=true;
            for(int i=0;i<2;++i){auto boxed=value_box(vc,&sentinel[i]);void* args[]={boxed,&i};ok&=invokeOk(set,arr,args);}
            size_t bytes=object_get_size(arr);
            if(ok&&bytes>=sizeof(sentinel)&&bytes<=256)for(size_t offset=16;offset+sizeof(sentinel)<=bytes;++offset){
                if(!std::memcmp(static_cast<unsigned char*>(arr)+offset,sentinel,sizeof(sentinel))){header=offset;break;}}
            gchandle_free(root);
        }
        log("Verified Vector3 array data offset="+std::to_string(header));
    }
    if(!header)return false;
    auto cls=object_get_class(renderer);auto bake=typedMethod(cls,"BakeMesh",{"UnityEngine.Mesh"});if(!bake)return false;
    auto now=GetTickCount64();BakedShape* shape=nullptr;
    for(auto& existing:bakedShapes)if(existing.renderer==renderer){shape=&existing;break;}
    if(!shape){bakedShapes.push_back({renderer,gchandle_new(renderer,false),0,now,{}});shape=&bakedShapes.back();}
    shape->seen=now;
    if(now-shape->updated>=66){
        shape->updated=now;shape->vertices.clear();
        if(!bakeMesh){auto meshClass=class_from_name(image("UnityEngine.CoreModule"),"UnityEngine","Mesh");
            auto ctor=meshClass?class_get_method_from_name(meshClass,".ctor",0):nullptr;
            if(!ctor)return false;bakeMesh=object_new(meshClass);if(!bakeMesh||!invokeOk(ctor,bakeMesh)){bakeMesh=nullptr;return false;}bakeMeshRoot=gchandle_new(bakeMesh,false);}
        void* args[]={bakeMesh};if(!invokeOk(bake,renderer,args))return false;
        auto verts=call(class_get_method_from_name(object_get_class(bakeMesh),"get_vertices",0),bakeMesh);if(!verts)return false;
        auto count=array_length(verts);if(!count||count>200000)return false;
        auto data=reinterpret_cast<const Vec*>(static_cast<const unsigned char*>(verts)+header);
        shape->vertices.assign(data,data+count);
    }
    bool any=false;
    for(auto point:shape->vertices){auto p=transformPoint(view,transformPoint(world,point));auto& m=projection.m;
        float w=m[3]*p.x+m[7]*p.y+m[11]*p.z+m[15];if(w<=.01f)continue;
        float x=((m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12])/w+1)*.5f*width;
        float y=(1-(m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13])/w)*.5f*height;
        if(!std::isfinite(x)||!std::isfinite(y))continue;
        entry.left=std::min(entry.left,x);entry.right=std::max(entry.right,x);entry.top=std::min(entry.top,y);entry.bottom=std::max(entry.bottom,y);any=true;
    }
    return any;
}
bool worldObjectBounds(void* root,void* cam,const Vec& center,int kind,int width,int height,WorldItem& item){
    struct Bounds{Vec center,extents;};
    item.left=item.top=1e30f;item.right=item.bottom=-1e30f;
    auto add=[&](const Bounds& b){bool projected=false;
        for(int corner=0;corner<8;++corner){
            Vec point{b.center.x+(corner&1?b.extents.x:-b.extents.x),b.center.y+(corner&2?b.extents.y:-b.extents.y),b.center.z+(corner&4?b.extents.z:-b.extents.z)},screen{};
            if(!project(cam,point,screen))continue;
            item.left=std::min(item.left,screen.x);item.right=std::max(item.right,screen.x);
            item.top=std::min(item.top,height-screen.y);item.bottom=std::max(item.bottom,height-screen.y);projected=true;
        }return projected;
    };
    bool found=false;
    for(auto renderer:renderers(root)){Bounds b{};
        if(!value(call(boundsMethod,renderer),b))continue;
        if(!std::isfinite(b.extents.x)||!std::isfinite(b.extents.y)||!std::isfinite(b.extents.z))continue;
        if(b.extents.x<.01f||b.extents.y<.01f||b.extents.x>30||b.extents.y>30||b.extents.z>30)continue;
        found|=add(b);
    }
    if(!found){Vec e=kind==3?Vec{.8f,1.45f,.8f}:kind==4?Vec{.85f,.85f,.85f}:Vec{1.4f,2.1f,.6f};
        found=add({{center.x,center.y+e.y,center.z},e});
    }
    if(!found||item.right-item.left<3||item.bottom-item.top<3)return false;
    if(item.right-item.left>width*.7f||item.bottom-item.top>height*.9f)return false;
    item.left=std::clamp(item.left,-width*.5f,width*1.5f);item.right=std::clamp(item.right,-width*.5f,width*1.5f);
    item.top=std::clamp(item.top,-height*.5f,height*1.5f);item.bottom=std::clamp(item.bottom,-height*.5f,height*1.5f);
    return true;
}
void finish() {
    stopping=true;clearSilent();noRecoilActive.store(false,std::memory_order_relaxed);noSpreadActive.store(false,std::memory_order_relaxed);weaknessActive.store(false,std::memory_order_relaxed);
    movementMultiplier.store(1);reloadMultiplier.store(1);fireMultiplier.store(1);removeGlow();clearBakedShapes();
    if(mappedHook){UnhookWindowsHookEx(mappedHook);mappedHook=nullptr;SetEvent(mappedStop);}
    if(shaderBundle){auto m=class_get_method_from_name(object_get_class(shaderBundle),"Unload",1);bool all=false;void* args[]={&all};invokeOk(m,shaderBundle,args);shaderBundle=nullptr;}
    if(silentInstalled){MH_DisableHook(silentCtrlAddress);MH_DisableHook(silentEnableAddress);MH_RemoveHook(silentCtrlAddress);MH_RemoveHook(silentEnableAddress);silentInstalled=false;}
    if(silentRayAddress){MH_DisableHook(silentRayAddress);MH_RemoveHook(silentRayAddress);silentRayAddress=nullptr;}
    if(watchRayAddress){MH_DisableHook(watchRayAddress);MH_RemoveHook(watchRayAddress);watchRayAddress=nullptr;}
    for(auto hook:projectileHooks){MH_DisableHook(hook);MH_RemoveHook(hook);}projectileHooks.clear();
    for(auto address:{cameraRecoilAddress,weaponRecoilAddress})if(address){MH_DisableHook(address);MH_RemoveHook(address);}
    for(auto address:{serverSpreadAddress,baseSpreadAddress,hitCtorAddress})if(address){MH_DisableHook(address);MH_RemoveHook(address);}
    for(auto address:{serverAimPositionAddress,baseAimPositionAddress,skillEndAddress,cameraPositionAddress})if(address){MH_DisableHook(address);MH_RemoveHook(address);}
    for(auto address:{moveSpeedAddress,fireSpeedAddress,reloadSpeedAddress})if(address){MH_DisableHook(address);MH_RemoveHook(address);}
    if(loopAddress){MH_DisableHook(loopAddress);MH_RemoveHook(loopAddress);loopAddress=nullptr;loopInstalled=false;}
    MH_Uninitialize();clearWorldCache();for(auto handle:roots)gchandle_free(handle);roots.clear();
    frame.shutdown=true;publish();
    if(shared){UnmapViewOfFile(shared);shared=nullptr;}
    if(mapping){CloseHandle(mapping);mapping=nullptr;}
    if(sharedMutex){CloseHandle(sharedMutex);sharedMutex=nullptr;}
}
void tick(bool probe) {
    static ULONGLONG lastTick=0,acquiredAt=0;static std::uint64_t pendingTarget=0;static bool previouslyActive=false;
    auto now=GetTickCount64();float dt=lastTick?std::clamp((now-lastTick)*.001f,.001f,.05f):1.f/60;lastTick=now;
    if(stopping)return;
    if(shared&&WaitForSingleObject(sharedMutex,0)==WAIT_OBJECT_0){cfg=shared->config;ReleaseMutex(sharedMutex);}
    sanitize(cfg);frame={};frame.ready=ready;clearSilent();
    int currentPlayer=0;if(mainCtrlField)field_static_get_value(mainCtrlField,&currentPlayer);
    localPlayerId.store(currentPlayer,std::memory_order_relaxed);
    noRecoilActive.store(cfg.noRecoil&&!cfg.menuOpen&&foreground(),std::memory_order_relaxed);
    noSpreadActive.store(cfg.noSpread&&!cfg.menuOpen&&foreground(),std::memory_order_relaxed);
    weaknessActive.store(cfg.weaknessHit&&!cfg.menuOpen&&foreground(),std::memory_order_relaxed);
    bool speedActive=!cfg.menuOpen&&foreground();
    movementMultiplier.store(speedActive?cfg.moveSpeed:1,std::memory_order_relaxed);
    reloadMultiplier.store(speedActive?cfg.reloadSpeed:1,std::memory_order_relaxed);
    fireMultiplier.store(speedActive?cfg.fireRate:1,std::memory_order_relaxed);
    frame.silentReady=silentInstalled;frame.glowReady=glowAvailable;
    bool key=(GetAsyncKeyState(cfg.key)&0x8000)!=0;
    if(!cfg.menuOpen&&foreground()&&key&&!wasKey)toggled=!toggled;
    wasKey=key;
    bool active=cfg.enabled&&!cfg.menuOpen&&foreground()&&(!cfg.onKey||cfg.keyMode==2||(cfg.keyMode==1?toggled:key));
    if(!cfg.silent&&cfg.legitOnlyFiring&&!(GetAsyncKeyState(VK_LBUTTON)&0x8000))active=false;
    if(scopedField)field_static_get_value(scopedField,&frame.scoped);
    bool iron=false;if(ironField)field_static_get_value(ironField,&iron);frame.scoped|=iron;
    if(cfg.scopedOnly&&!frame.scoped)active=false;
    if(!active)lockedId=0;
    if(!probe&&cfg.silent&&!silentAttempted){silentAttempted=true;frame.silentReady=installSilent();}
    if(!probe&&!combatHooksAttempted){combatHooksAttempted=true;installCombatHooks();}
    frame.silentRayReady=silentRayAddress!=nullptr;frame.silentRedirects=silentRedirects.load();
    frame.loopReady=loopInstalled;
    frame.loopTicks=pumpTicks;
    frame.silentFamilies=(silentInstalled?1:0)+(throwReady?1:0)+(parabolaReady?1:0);
    frame.rayCalls=rayCalls.load();frame.raycastCalls=raycastCalls.load();frame.throwCalls=throwCalls.load();frame.parabolaCalls=parabolaCalls.load();
    frame.raycastRedirects=raycastRedirects.load();frame.projectileRedirects=projectileRedirects.load();
    frame.aimPositionCalls=aimPositionCalls.load();frame.aimPositionRedirects=aimPositionRedirects.load();
    frame.spreadCalls=spreadCalls.load();frame.spreadLocalCalls=spreadLocalCalls.load();frame.hitInfoCalls=hitInfoCalls.load();
    frame.straightShots=straightShots.load();
    frame.weaknessRedirects=weaknessRedirects.load();
    for(int i=0;i<8;++i)frame.targetTypes[i]=targetTypes[i].load();
    auto list=call(monstersMethod),cam=call(cameraMethod);
    if(!list||!cam){if(glowMaterial)removeGlow();std::strcpy(frame.status,"Wejdz w rozgrywke - brak aktywnej kamery/celow.");publish();return;}
    auto cls=object_get_class(list);auto countMethod=class_get_method_from_name(cls,"get_Count",0);auto itemMethod=class_get_method_from_name(cls,"get_Item",1);
    int count=0,w=0,h=0;
    if(!value(call(countMethod,list),count)||!itemMethod||count<0||count>4096)return;
    if(!value(call(widthMethod),w)||!value(call(heightMethod),h)||w<=0||h<=0)return;
    frame.width=w;frame.height=h;
    Matrix4 view{},projection{};auto camClass=object_get_class(cam);
    bool precise=cfg.preciseBoxes&&value(call(class_get_method_from_name(camClass,"get_worldToCameraMatrix",0),cam),view)
        &&value(call(class_get_method_from_name(camClass,"get_projectionMatrix",0),cam),projection);
    auto transMethod=class_get_method_from_name(object_get_class(cam),"get_transform",0);
    auto transform=call(transMethod,cam);Vec origin{};
    if(!transform||!value(call(positionMethod,transform),origin))return;
    static ULONGLONG lastWorldScan=0,lastPickup=0;static int dropCache=0,pickupTotal=0;
    if(probe||((cfg.itemEsp||cfg.autoPickup)&&now-lastWorldScan>200)){
        lastWorldScan=now;clearWorldCache();dropCache=0;auto players=allPlayers(probe);
        Vec pickupPos{};bool canPickup=false;static bool pickupDiagnostic=false;
        if(cfg.autoPickup&&!cfg.menuOpen&&foreground()&&now-lastPickup>500){
            auto mgr=class_from_name(image("Assembly-CSharp"),"","NewPlayerManager");
            int id=localPlayerId.load(std::memory_order_relaxed);
            auto getPlayer=mgr?class_get_method_from_name(mgr,"GetPlayer",1):nullptr;
            void* args[]={&id};auto player=id>0?call(getPlayer,nullptr,args):nullptr;
            auto playerTrans=player?fieldObject(player,"gameTrans"):nullptr;
            canPickup=playerTrans&&value(call(positionMethod,playerTrans),pickupPos);
            if(!pickupDiagnostic){log("Pickup main="+std::to_string(id)+" player="+std::to_string(player!=nullptr)+" transform="+std::to_string(playerTrans!=nullptr)+" position="+std::to_string(canPickup));pickupDiagnostic=true;}
            pickupPos.y+=.5f;lastPickup=now;
        }
        else if(cfg.autoPickup&&!pickupDiagnostic){log("Pickup inactive: menu="+std::to_string(cfg.menuOpen)+" foreground="+std::to_string(foreground()));pickupDiagnostic=true;}
        auto setPos=method(image("UnityEngine.CoreModule"),"UnityEngine","Transform","set_position",1);
        auto manager=class_from_name(image("Assembly-CSharp"),"","NewPlayerManager");
        void* hiddenDoors=nullptr,*transferDoors=nullptr;
        if(auto field=manager?class_get_field_from_name(manager,"NotOpenHideDoorLst"):nullptr)field_static_get_value(field,&hiddenDoors);
        if(auto field=manager?class_get_field_from_name(manager,"TransferDoorList"):nullptr)field_static_get_value(field,&transferDoors);
        for(auto object:players){auto drop=fieldObject(object,"DropOPCom"),npc=fieldObject(object,"NPCOPCom");
            auto root=fieldObject(object,"gameTrans");if(!root)continue;
            if(drop){++dropCache;
                if(probe)log(std::string("Drop class=")+class_get_namespace(object_get_class(drop))+"."+class_get_name(object_get_class(drop)));
                if(canPickup&&setPos){void* args[]={&pickupPos};if(invokeOk(setPos,root,args))++pickupTotal;else if(pickupTotal==0)log("Pickup Transform.set_position failed.");}
            }
            if(!npc)continue;
            auto cls=class_get_name(object_get_class(npc));if(!cls)continue;
            int kind=0;const char* label=nullptr;
            if(std::strstr(cls,"Shop")||std::strstr(cls,"Smith")||std::strstr(cls,"Store")){kind=3;label="Merchant";}
            else if(std::strstr(cls,"PassBox")||std::strstr(cls,"Transfer")){kind=2;label="Portal";}
            else if(std::strstr(cls,"Box")){kind=4;label="Chest";}
            else if(std::strstr(cls,"Door")){
                int objectId=0,sid=0;
                if(auto field=class_get_field_from_name(object_get_class(object),"ObjectID"))field_get_value(object,field,&objectId);
                if(auto field=class_get_field_from_name(object_get_class(object),"SID"))field_get_value(object,field,&sid);
                bool portal=collectionHasInt(hiddenDoors,objectId)||collectionHasInt(hiddenDoors,sid)
                    ||collectionHasInt(transferDoors,objectId)||collectionHasInt(transferDoors,sid);
                kind=portal?2:1;label=portal?"Portal":"Dungeon";
            }
            if(probe){int sid=0;auto field=class_get_field_from_name(object_get_class(object),"SID");if(field)field_get_value(object,field,&sid);
                log(std::string("NPC class=")+class_get_namespace(object_get_class(npc))+"."+cls+" SID="+std::to_string(sid)+" kind="+std::to_string(kind));}
            if(!kind||worldCache.size()>=kMaxItems)continue;
            auto handle=gchandle_new(root,false);
            if(handle)worldCache.push_back({root,handle,kind,label});
        }
        if(probe)log("PlayerDict objects="+std::to_string(players.size())+" drops="+std::to_string(dropCache)+" NPCs="+std::to_string(worldCache.size()));
    }
    frame.nearbyDrops=dropCache;frame.pickupMoves=pickupTotal;
    // Object discovery is cached, but screen projection and renderer bounds follow the camera every tick.
    if(cfg.itemEsp||probe)for(const auto& candidate:worldCache){
        if(frame.itemCount>=kMaxItems)break;
        Vec world{},screen{};
        if(!value(call(positionMethod,candidate.transform),world)||!project(cam,world,screen))continue;
        float dx=world.x-origin.x,dy=world.y-origin.y,dz=world.z-origin.z;
        WorldItem item{};item.x=screen.x;item.y=h-screen.y;item.distance=std::sqrt(dx*dx+dy*dy+dz*dz);item.kind=candidate.kind;
        item.visible=visible(origin,world,candidate.transform);
        item.hasBounds=worldObjectBounds(candidate.transform,cam,world,candidate.kind,w,h,item);
        std::snprintf(item.name,sizeof(item.name),"%s",candidate.label);
        frame.items[frame.itemCount++]=item;
    }
    float cameraFov=60;value(call(cameraFovMethod,cam),cameraFov);
    if(!std::isfinite(cameraFov)||cameraFov<=1||cameraFov>=179)cameraFov=60;
    frame.fovRadius=std::tan(cfg.fov*0.5f*3.14159265f/180)*h*.5f/std::tan(cameraFov*.5f*3.14159265f/180);
    ++glowTick;bool glow=!probe&&prepareGlow(cam);frame.glowReady=glowAvailable;
    int bestIndex=-1;float best=1e30f;Vec bestWorld{};
    for(int i=0;i<count&&frame.count<kMaxEnemies;++i){
        void* idx[]={&i};auto enemy=call(itemMethod,list,idx);if(!enemy)continue;
        auto enemyClass=object_get_class(enemy);bool alive=true;
        value(call(class_get_method_from_name(enemyClass,"IsLive",0),enemy),alive);if(!alive)continue;
        auto part=body(enemy);if(!part)continue;
        bool nearest=true,needActive=true,lockFlaw=false;void* weakArgs[]={&nearest,&needActive,&lockFlaw};
        auto weakspot=call(weakMethod,part,weakArgs);
        auto weak=cfg.bone==1?call(centerMethod,part):cfg.bone==2?bodyNode(part,"headWeak"):cfg.bone==3?bodyNode(part,"headCenter"):weakspot;
        if(!weak)weak=call(centerMethod,part);if(!weak)continue;
        Vec pos{},screen{};if(!value(call(positionMethod,weak),pos))continue;
        Vec delta{pos.x-origin.x,pos.y-origin.y,pos.z-origin.z};float dist=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
        float range=std::max(cfg.maxDistance,std::max(cfg.esp?cfg.espDistance:0.f,cfg.glow?cfg.glowDistance:0.f));
        if(!std::isfinite(dist)||dist<.2f||dist>range)continue;
        auto root=fieldObject(enemy,"gameTrans");if(!root)root=fieldObject(enemy,"GameTrans");if(!root)continue;
        auto rs=renderers(root);
        frame.rendererCount+=static_cast<int>(rs.size());
        if(glow&&dist<=cfg.glowDistance)for(auto r:rs)frame.glowDrawCount+=applyGlow(r);
        if(!project(cam,pos,screen))continue;
        Enemy entry{};entry.id=reinterpret_cast<std::uint64_t>(enemy);entry.x=screen.x;entry.y=h-screen.y;entry.distance=dist;entry.hasWeakspot=weakspot!=nullptr;
        if(weakspot){Vec weakWorld{},weakScreen{};if(value(call(positionMethod,weakspot),weakWorld)&&project(cam,weakWorld,weakScreen)){entry.weakX=weakScreen.x;entry.weakY=h-weakScreen.y;}else entry.hasWeakspot=false;}
        entry.visible=visible(origin,pos,root);
        auto prop=fieldObject(enemy,"playerProp");
        if(prop){auto pc=object_get_class(prop);auto ratio=[&](const char* current,const char* maximum){
            std::int64_t a=0,b=0;if(!value(call(class_get_method_from_name(pc,current,0),prop),a)||!value(call(class_get_method_from_name(pc,maximum,0),prop),b)||b<=0)return -1.f;
            return std::clamp(static_cast<float>(static_cast<double>(a)/b),0.f,1.f);};
            entry.health=ratio("get_HP","get_HPMax");entry.shield=ratio("get_Shield","get_ShieldMax");entry.armor=ratio("get_Armor","get_ArmorMax");
        }
        // PlayerProp.Name can be the owner's nickname. Resolve the monster SID
        // through the game's current localization table, including after updates.
        int sid=0;auto sidField=class_get_field_from_name(enemyClass,"SID");if(sidField)field_get_value(enemy,sidField,&sid);
        static std::unordered_map<int,std::string> displayNames;
        auto found=displayNames.find(sid);
        if(found==displayNames.end()&&monsterNameMethod&&sid>0){void* skill=nullptr;void* args[]={skill,&sid};auto name=call(monsterNameMethod,nullptr,args);
            std::string resolved;if(name&&string_length(name)>0&&string_length(name)<256){resolved.resize(256);int n=WideCharToMultiByte(CP_UTF8,0,string_chars(name),string_length(name),resolved.data(),255,nullptr,nullptr);resolved.resize(std::max(0,n));}
            if(!resolved.empty())found=displayNames.emplace(sid,std::move(resolved)).first;
        }
        if(found!=displayNames.end())std::snprintf(entry.name,sizeof(entry.name),"%s",found->second.c_str());
        else std::snprintf(entry.name,sizeof(entry.name),"Przeciwnik #%d",sid);
        if(probe)log(std::string("Monster SID=")+std::to_string(sid)+" display="+entry.name);
        bool hasBounds=false;entry.left=1e30f;entry.top=1e30f;entry.right=-1e30f;entry.bottom=-1e30f;
        struct Bounds{Vec center,extents;};
        bool hasSkinned=std::any_of(rs.begin(),rs.end(),[](void* r){return !std::strcmp(class_get_name(object_get_class(r)),"SkinnedMeshRenderer");});
        for(auto r:rs){Bounds b{};if(!value(call(boundsMethod,r),b))continue;
            if(hasSkinned&&std::strcmp(class_get_name(object_get_class(r)),"SkinnedMeshRenderer"))continue;
            if(probe){auto nm=call(nameMethod,r);std::string label;
                if(nm){std::wstring ws(string_chars(nm),string_length(nm));label.assign(ws.begin(),ws.end());}
                log(std::string("Mesh ")+class_get_name(object_get_class(r))+" "+label+" extents="+std::to_string(b.extents.x)+","+std::to_string(b.extents.y)+","+std::to_string(b.extents.z));}
            auto localBounds=class_get_method_from_name(object_get_class(r),"get_localBounds",0);
            auto rTransform=call(class_get_method_from_name(object_get_class(r),"get_transform",0),r);
            Matrix4 matrix{};Bounds local{};
            bool hasMatrix=rTransform&&value(call(class_get_method_from_name(object_get_class(rTransform),"get_localToWorldMatrix",0),rTransform),matrix);
            if(precise&&hasMatrix&&preciseBounds(r,matrix,view,projection,entry,w,h)){hasBounds=true;++frame.preciseCount;continue;}
            bool useLocal=localBounds&&hasMatrix&&value(call(localBounds,r),local);
            if(useLocal)b=local;
            for(int corner=0;corner<8;++corner){Vec p{b.center.x+(corner&1?b.extents.x:-b.extents.x),b.center.y+(corner&2?b.extents.y:-b.extents.y),b.center.z+(corner&4?b.extents.z:-b.extents.z)},s{};
                if(useLocal){auto& m=matrix.m;p={m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};}
                if(!project(cam,p,s))continue;
                entry.left=std::min(entry.left,s.x);entry.right=std::max(entry.right,s.x);entry.top=std::min(entry.top,h-s.y);entry.bottom=std::max(entry.bottom,h-s.y);hasBounds=true;
            }
        }
        entry.hasBounds=hasBounds;
        if(!hasBounds){entry.left=entry.right=entry.x;entry.top=entry.bottom=entry.y;}
        int out=frame.count++;frame.enemies[out]=entry;
        float dx=entry.x-w*.5f,dy=entry.y-h*.5f,d2=dx*dx+dy*dy;
        if(dist>cfg.maxDistance||(cfg.bone==0&&cfg.strictWeakspot&&!weakspot)||d2>frame.fovRadius*frame.fovRadius||(cfg.visibleOnly&&!entry.visible))continue;
        float score=cfg.priority==1?dist:d2;
        if(lockedId==entry.id){if(cfg.aimlock)score=-1;else score*=1-cfg.legitStickiness*.009f;}
        if(score<best){best=score;bestIndex=out;bestWorld=pos;}
    }
    for(auto it=overrides.begin();it!=overrides.end();){
        if(it->seen!=glowTick){auto set=class_get_method_from_name(object_get_class(it->renderer),"set_sharedMaterials",1);void* args[]={it->materials};invokeOk(set,it->renderer,args);gchandle_free(it->rendererRoot);gchandle_free(it->materialsRoot);it=overrides.erase(it);}else ++it;
    }
    for(auto it=bakedShapes.begin();it!=bakedShapes.end();){if(now-it->seen>2000){gchandle_free(it->root);it=bakedShapes.erase(it);}else ++it;}
    // Triggerbot uses the current projected hit area; it does not move the camera.
    static std::uint64_t triggerCandidate=0;static ULONGLONG triggerSince=0,lastTrigger=0;
    std::uint64_t pointed=0;
    if(cfg.triggerEnabled&&!cfg.menuOpen&&foreground()&&(!cfg.triggerOnKey||(GetAsyncKeyState(cfg.triggerKey)&0x8000))) {
        for(int i=0;i<frame.count;++i){const auto& e=frame.enemies[i];
            if(!e.hasBounds||e.distance>cfg.triggerDistance||(cfg.triggerVisibleOnly&&!e.visible))continue;
            bool inside=cfg.triggerWeakspot?e.hasWeakspot&&std::hypot(e.weakX-w*.5f,e.weakY-h*.5f)<std::clamp((e.right-e.left)*.08f,6.f,20.f)
                :w*.5f>=e.left&&w*.5f<=e.right&&h*.5f>=e.top&&h*.5f<=e.bottom;
            if(inside){pointed=e.id;break;}
        }
    }
    if(pointed!=triggerCandidate){triggerCandidate=pointed;triggerSince=now;}
    if(pointed&&now-triggerSince>=cfg.triggerDelayMs&&now-lastTrigger>=cfg.triggerCooldownMs&&!(GetAsyncKeyState(VK_LBUTTON)&0x8000)){
        INPUT click[2]{};for(auto& in:click)in.type=INPUT_MOUSE;
        click[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;click[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
        if(SendInput(2,click,sizeof(INPUT))==2)lastTrigger=now;
    }
    if(bestIndex>=0){frame.enemies[bestIndex].selected=true;if(active)lockedId=frame.enemies[bestIndex].id;}else lockedId=0;
    std::snprintf(frame.status,sizeof(frame.status),"Cele: %d | %s",frame.count,bestIndex>=0?"cel wybrany":"brak celu w FOV");
    if(probe){log(frame.status);log("Render/model API: "+std::to_string(glowAvailable)+"; renderers="+std::to_string(frame.rendererCount));
        for(int i=0;i<frame.count;++i){auto& e=frame.enemies[i];log(std::string("ESP ")+e.name+" visible="+std::to_string(e.visible)+" box="+std::to_string(e.left)+","+std::to_string(e.top)+","+std::to_string(e.right)+","+std::to_string(e.bottom)+" hp="+std::to_string(e.health)+" shield="+std::to_string(e.shield)+" armor="+std::to_string(e.armor));}
        publish();return;}
    auto targetId=bestIndex>=0?frame.enemies[bestIndex].id:0;
    bool switched=pendingTarget&&pendingTarget!=targetId;
    static float waitMs=0;
    if(!active||!previouslyActive||targetId!=pendingTarget){
        acquiredAt=now;waitMs=switched?cfg.switchMs:cfg.reactionMs;
        if(!cfg.silent&&targetId){std::uint32_t seed=static_cast<std::uint32_t>(targetId^(targetId>>32)^now);seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
            waitMs+=cfg.reactionJitterMs*(seed&1023)/1023.f;}
        pendingTarget=active?targetId:0;
    }
    previouslyActive=active;
    if(now-acquiredAt<waitMs){publish();return;}
    if(active&&bestIndex>=0){
        frame.silentTargetAvailable=true;
        if(cfg.silent){if(silentInstalled){silentHoldKey.store(cfg.onKey&&cfg.keyMode==0?cfg.key:0,std::memory_order_relaxed);AcquireSRWLockExclusive(&silentLock);silentPosition=bestWorld;silentTime=GetTickCount64();ReleaseSRWLockExclusive(&silentLock);}}
        else {
            auto& e=frame.enemies[bestIndex];INPUT in{};in.type=INPUT_MOUSE;in.mi.dwFlags=MOUSEEVENTF_MOVE;
            float elapsed=static_cast<float>(now-acquiredAt)-waitMs;
            float ramp=cfg.legitAccelerationMs>0?std::clamp(elapsed/cfg.legitAccelerationMs,0.f,1.f):1.f;
            float variation=1+cfg.legitVariation*.5f*(std::sin(now*.011f)+std::sin(now*.0061f+1.3f));
            auto step=[&](float d,float axis)->LONG{
                if(std::abs(d)<=cfg.deadzone)return 0;
                float blend=1-std::pow(1-1/cfg.smooth,dt*60);
                float proximity=std::clamp(std::abs(d)/std::max(frame.fovRadius,1.f),.3f,1.f);
                float curve=std::pow(proximity,cfg.legitCurve);
                float limit=cfg.aimSpeed*dt*axis*ramp*variation;
                float v=std::clamp(d*blend*axis*curve*ramp*variation,-limit,limit);
                return static_cast<LONG>(std::round(v));
            };
            in.mi.dx=step(e.x-w*.5f,cfg.legitHorizontal);in.mi.dy=step(e.y-h*.5f,cfg.legitVertical);
            if(in.mi.dx||in.mi.dy)SendInput(1,&in,sizeof(in));
        }
    }
    publish();
}
void captureGame(int number){
    auto m=method(image("UnityEngine.ScreenCaptureModule"),"UnityEngine","ScreenCapture","CaptureScreenshot",1);
    const char* file=number==5?"glow-test.png":number==7?"glow-color.png":"glow-restored.png";
    void* args[]={string_new((directory+file).c_str())};invokeOk(m,nullptr,args);
}
void pumpGame(){
    if(stopping||GetCurrentThreadId()!=mappedGameThread||pumping.test_and_set(std::memory_order_acquire))return;
    static ULONGLONG last=0;auto now=GetTickCount64();
    if(now-last<8){pumping.clear(std::memory_order_release);return;}last=now;
    unsigned command=0;
    if(shared&&WaitForSingleObject(sharedMutex,0)==WAIT_OBJECT_0){command=shared->command;shared->command=0;ReleaseMutex(sharedMutex);}
    if(command==4){finish();pumping.clear(std::memory_order_release);return;}
    if(command==3)dumpRelevant();
    if(command>=5&&command<=7)captureGame(command);
    ++pumpTicks;
    tick(command==1);
    if(mappedLoopReady&&pumpTicks>=3)SetEvent(mappedLoopReady);
    pumping.clear(std::memory_order_release);
}
float hookTime(void* info){float result=originalTime(info);pumpGame();return result;}
bool installGameLoop(){
    if(loopInstalled)return true;
    auto m=method(image("UnityEngine.CoreModule"),"UnityEngine","Time","get_deltaTime",0);
    if(!m)return false;loopAddress=*static_cast<void**>(m);
    auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(MH_CreateHook(loopAddress,reinterpret_cast<void*>(hookTime),reinterpret_cast<void**>(&originalTime))!=MH_OK)return false;
    if(MH_EnableHook(loopAddress)!=MH_OK){MH_RemoveHook(loopAddress);loopAddress=nullptr;return false;}
    loopInstalled=true;log("Manual map Time.get_deltaTime hook installed.");return true;
}
