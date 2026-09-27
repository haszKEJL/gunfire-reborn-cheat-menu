// Additional projectile families; signatures are matched against live metadata.
struct Vec2 {float x,y;};
using ThrowFn=void(*)(void*,void*,Vec*,Vec*,float,float,Vec*,Vec2,int,bool,bool,void*,float,void*,float,float,int,bool,Vec2,int,int,float,float,bool,int,bool,float,float,bool,float,float,void*);
using ParabolaFn=void(*)(void*,void*,Vec*,Vec*,float,float,Vec*,Vec2,Vec2,int,bool,bool,int,bool,int,int,int,float,bool,bool,float,float,bool,bool,void*);
ThrowFn originalThrow=nullptr;ParabolaFn originalParabola=nullptr;
std::vector<void*> projectileHooks;
bool throwReady=false,parabolaReady=false;
bool redirectDirection(Vec* start,Vec& direction,int target){
    Vec point{};if(target!=1||!start||!silentTarget(point))return false;
    Vec delta{point.x-start->x,point.y-start->y,point.z-start->z};float length=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
    if(!std::isfinite(length)||length<.01f)return false;
    direction={delta.x/length,delta.y/length,delta.z/length};++silentRedirects;++projectileRedirects;return true;
}
bool straightProjectileDirection(void* skill,Vec& direction,int target){
    if(target!=1||!noSpreadActive.load(std::memory_order_relaxed)||!originalRay||!localSkill(skill))return false;
    auto ray=originalRay(nullptr);float length=std::hypot(ray.direction.x,ray.direction.y,ray.direction.z);
    if(!std::isfinite(length)||length<.01f)return false;
    direction={ray.direction.x/length,ray.direction.y/length,ray.direction.z/length};++straightShots;return true;
}
void hookThrow(void* self,void* skill,Vec* start,Vec* dir,float speed,float radius,Vec* force,Vec2 bounce,int delay,bool hitOver,bool hitStatic,
    void* grenade,float life,void* effect,float effectLife,float inner,int pierce,bool ignore,Vec2 unitBounce,int target,int overType,
    float change,float maximum,bool ignorePetro,int trigger,bool ignoreShield,float weakAngle,float weakDistance,bool pierceWeak,float fall,float threshold,void* info){
    ++throwCalls;Vec adjusted{};auto direction=(redirectDirection(start,adjusted,target)||straightProjectileDirection(skill,adjusted,target))?&adjusted:dir;
    originalThrow(self,skill,start,direction,speed,radius,force,bounce,delay,hitOver,hitStatic,grenade,life,effect,effectLife,inner,pierce,ignore,unitBounce,target,overType,change,maximum,ignorePetro,trigger,ignoreShield,weakAngle,weakDistance,pierceWeak,fall,threshold,info);
}
void hookParabola(void* self,void* skill,Vec* start,Vec* dir,float speed,float radius,Vec* acceleration,Vec2 bounce,Vec2 unitBounce,int delay,
    bool hitOver,bool hitStatic,int pierce,bool ignore,int summonId,int summonSid,int target,float distance,bool hitHero,bool forceSpeed,
    float inner,float scale,bool hitMonster,bool warning,void* info){
    ++parabolaCalls;Vec adjusted{};auto direction=(redirectDirection(start,adjusted,target)||straightProjectileDirection(skill,adjusted,target))?&adjusted:dir;
    originalParabola(self,skill,start,direction,speed,radius,acceleration,bounce,unitBounce,delay,hitOver,hitStatic,pierce,ignore,summonId,summonSid,target,distance,hitHero,forceSpeed,inner,scale,hitMonster,warning,info);
}
void installProjectileHooks(void* game){
    auto install=[&](const char* cls,std::initializer_list<const char*> signature,void* hook,void** original){
        auto c=class_from_name(game,"SkillBolt",cls);auto m=c?typedMethod(c,"EnableCtrl",signature):nullptr;
        if(!m){log(std::string("Silent unsupported signature: ")+cls);return false;}
        auto address=*static_cast<void**>(m);
        if(MH_CreateHook(address,hook,original)!=MH_OK)return false;
        if(MH_EnableHook(address)!=MH_OK){MH_RemoveHook(address);return false;}
        projectileHooks.push_back(address);log(std::string("Silent projectile hook: ")+cls);return true;
    };
    throwReady=install("ThrowByPowerCartoon",{
        "SkillBolt.CSkillBase","UnityEngine.Vector3","UnityEngine.Vector3","System.Single","System.Single","UnityEngine.Vector3","UnityEngine.Vector2","System.Int32","System.Boolean","System.Boolean","UnityEngine.Object","System.Single","UnityEngine.Object","System.Single","System.Single","System.Int32","System.Boolean","UnityEngine.Vector2","GO_ENUM.OBJ_TYPE","GO_ENUM.HIT_OVER_TYPE","System.Single","System.Single","System.Boolean","System.Int32","System.Boolean","System.Single","System.Single","System.Boolean","System.Single","System.Single"
    },reinterpret_cast<void*>(hookThrow),reinterpret_cast<void**>(&originalThrow));
    parabolaReady=install("EntityParabolaCartoon",{
        "SkillBolt.CSkillBase","UnityEngine.Vector3","UnityEngine.Vector3","System.Single","System.Single","UnityEngine.Vector3","UnityEngine.Vector2","UnityEngine.Vector2","System.Int32","System.Boolean","System.Boolean","System.Int32","System.Boolean","System.Int32","System.Int32","GO_ENUM.OBJ_TYPE","System.Single","System.Boolean","System.Boolean","System.Single","System.Single","System.Boolean","System.Boolean"
    },reinterpret_cast<void*>(hookParabola),reinterpret_cast<void**>(&originalParabola));
}
