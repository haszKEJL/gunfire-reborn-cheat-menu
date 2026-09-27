#pragma once
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <string>
constexpr unsigned kVersion=6;
constexpr int kMaxEnemies=256;
constexpr int kMaxItems=128;
struct MapBootstrap { DWORD thread; DWORD testOnly; char directory[32768]; };
constexpr wchar_t kMessage[]=L"GunfireReborn.ScarletAim.v6";
struct Config {
    bool enabled=true,onKey=true,aimlock=false,silent=false,visibleOnly=true,scopedOnly=false;
    int key=VK_LSHIFT,keyMode=0,bone=0,priority=0;
    float fov=12.0f,smooth=8.0f,maxDistance=150.0f;
    float deadzone=2,aimSpeed=1800,reactionMs=100,switchMs=150;
    bool strictWeakspot=false;
    bool noSpread=false,noRecoil=false,legacyReserved=false,weaknessHit=false;
    bool triggerEnabled=false,triggerOnKey=false,triggerWeakspot=false,triggerVisibleOnly=true;
    int triggerKey=VK_XBUTTON1;
    float triggerDelayMs=75,triggerCooldownMs=150,triggerDistance=150;
    bool autoPickup=false,itemEsp=false,itemDungeon=true,itemMerchant=true,itemChest=true;
    bool esp=true,boxes=true,names=true,distance=true,lines=false,weakPoint=true,fovCircle=true;
    bool espVisibleOnly=false,glow=false,glowThroughWalls=true;
    float glowStrength=.8f,glowDistance=150,espDistance=150,boxThickness=1.2f,fillAlpha=.06f;
    int boxStyle=1,lineOrigin=2;
    bool boxFill=false;
    bool healthBar=true,shieldBar=true,armorBar=true;
    bool preciseBoxes=true;
    float glowColor[4]={.65f,.3f,1,.8f};
    float visibleColor[4]={0.25f,0.9f,0.55f,1};
    float hiddenColor[4]={1,0.3f,0.25f,1};
    bool menuOpen=true;
    int language=0;
    bool itemPortal=true,itemBoxes=true,itemNames=true,itemDistance=true,itemLines=false,itemFill=false;
    float itemEspDistance=200,itemBoxThickness=1.5f,itemFillAlpha=.07f;
    float itemDungeonColor[4]={.63f,.40f,.96f,1},itemPortalColor[4]={.35f,.72f,1,1};
    float itemMerchantColor[4]={.98f,.76f,.35f,1},itemChestColor[4]={.32f,.88f,.80f,1};
    bool espOutline=true,espCenterDot=false,espHeadDot=false,espSelectedHighlight=true,espOffscreenArrows=false;
    int namePosition=0;
    float espTextScale=1,boxRounding=0,offscreenArrowSize=12;
    bool legitOnlyFiring=false;
    float legitHorizontal=1,legitVertical=1,legitAccelerationMs=0,legitCurve=0;
    float legitVariation=0,legitStickiness=0,reactionJitterMs=0;
    float moveSpeed=1,reloadSpeed=1,fireRate=1;
};
inline void sanitize(Config& c) {
    auto limit=[](float& f,float lo,float hi,float fallback){ f=std::isfinite(f)?std::clamp(f,lo,hi):fallback; };
    c.legacyReserved=false;
    c.key=std::clamp(c.key,1,254); c.keyMode=std::clamp(c.keyMode,0,2);
    c.bone=std::clamp(c.bone,0,3);c.priority=std::clamp(c.priority,0,1);
    limit(c.fov,1,179,12);limit(c.smooth,1,50,8);limit(c.maxDistance,5,500,150);limit(c.glowStrength,.1f,3,.8f);
    limit(c.glowDistance,5,500,150);limit(c.espDistance,5,500,150);limit(c.boxThickness,.5f,4,1.2f);limit(c.fillAlpha,0,.5f,.06f);
    limit(c.deadzone,0,30,2);limit(c.aimSpeed,50,6000,1800);limit(c.reactionMs,0,1000,100);limit(c.switchMs,0,1000,150);
    c.triggerKey=std::clamp(c.triggerKey,1,254);limit(c.triggerDelayMs,0,500,75);limit(c.triggerCooldownMs,50,1000,150);limit(c.triggerDistance,5,500,150);
    c.boxStyle=std::clamp(c.boxStyle,0,2);c.lineOrigin=std::clamp(c.lineOrigin,0,2);
    c.language=std::clamp(c.language,0,2);c.namePosition=std::clamp(c.namePosition,0,1);
    limit(c.itemEspDistance,5,500,200);limit(c.itemBoxThickness,.5f,4,1.5f);limit(c.itemFillAlpha,0,.5f,.07f);
    limit(c.espTextScale,.7f,1.8f,1);limit(c.boxRounding,0,16,0);limit(c.offscreenArrowSize,5,30,12);
    limit(c.legitHorizontal,.1f,2,1);limit(c.legitVertical,.1f,2,1);limit(c.legitAccelerationMs,0,1000,0);
    limit(c.legitCurve,0,2,0);limit(c.legitVariation,0,.25f,0);limit(c.legitStickiness,0,100,0);limit(c.reactionJitterMs,0,250,0);
    limit(c.moveSpeed,1,3,1);limit(c.reloadSpeed,1,3,1);limit(c.fireRate,1,3,1);
    for(auto p:{c.visibleColor,c.hiddenColor,c.glowColor,c.itemDungeonColor,c.itemPortalColor,c.itemMerchantColor,c.itemChestColor})for(int i=0;i<4;++i)limit(p[i],0,1,1);
}
struct Enemy {
    std::uint64_t id=0;
    float x=0,y=0,weakX=0,weakY=0,left=0,top=0,right=0,bottom=0,distance=0;
    bool visible=false,selected=false,hasBounds=false,hasWeakspot=false;
    float health=-1,shield=-1,armor=-1;
    char name[80]{};
};
struct WorldItem {float x=0,y=0,left=0,top=0,right=0,bottom=0,distance=0;int kind=0;bool hasBounds=false,visible=false;char name[64]{};};
struct Frame {
    ULONGLONG time=0;
    int width=0,height=0,count=0,rendererCount=0,glowDrawCount=0,preciseCount=0;
    int itemCount=0,nearbyDrops=0,pickupMoves=0;
    float fovRadius=0;
    bool ready=false,scoped=false,silentReady=false,glowReady=false,shutdown=false;
    bool silentRayReady=false;
    unsigned silentRedirects=0;
    int silentFamilies=0;
    unsigned rayCalls=0,raycastCalls=0,throwCalls=0,parabolaCalls=0;
    unsigned raycastRedirects=0,projectileRedirects=0;
    unsigned aimPositionCalls=0,aimPositionRedirects=0;
    unsigned spreadCalls=0,spreadLocalCalls=0,hitInfoCalls=0;
    unsigned straightShots=0;
    unsigned weaknessRedirects=0;
    unsigned targetTypes[8]{};
    bool silentTargetAvailable=false;
    bool loopReady=false;
    unsigned loopTicks=0;
    char status[192]{};
    Enemy enemies[kMaxEnemies]{};
    WorldItem items[kMaxItems]{};
};
struct Shared { unsigned version=kVersion; Config config{}; Frame frame{}; unsigned command=0,commandSerial=0; };
inline std::wstring sharedName(DWORD pid,const wchar_t* suffix) {
    return L"Local\\GunfireReborn.ScarletAim.v6."+std::to_wstring(pid)+suffix;
}
