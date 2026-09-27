#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include "overlay.hpp"
#include "../vendor/imgui/imgui.h"
#include "../vendor/imgui/backends/imgui_impl_win32.h"
#include "../vendor/imgui/backends/imgui_impl_dx11.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
namespace {
ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;IDXGISwapChain* swapchain=nullptr;ID3D11RenderTargetView* targetView=nullptr;
bool quit=false;
void releaseTarget(){if(targetView){targetView->Release();targetView=nullptr;}}
bool createTarget(){ID3D11Texture2D* buffer=nullptr;if(FAILED(swapchain->GetBuffer(0,IID_PPV_ARGS(&buffer))))return false;
    auto hr=device->CreateRenderTargetView(buffer,nullptr,&targetView);buffer->Release();return SUCCEEDED(hr);}
LRESULT CALLBACK windowProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(ImGui::GetCurrentContext()&&ImGui_ImplWin32_WndProcHandler(w,msg,wp,lp))return 1;
    if(msg==WM_SIZE&&device&&wp!=SIZE_MINIMIZED){releaseTarget();if(SUCCEEDED(swapchain->ResizeBuffers(0,LOWORD(lp),HIWORD(lp),DXGI_FORMAT_UNKNOWN,0)))createTarget();return 0;}
    if(msg==WM_DESTROY){quit=true;return 0;}return DefWindowProcW(w,msg,wp,lp);
}
bool createD3D(HWND w){
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferCount=2;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.OutputWindow=w;desc.SampleDesc.Count=1;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL level;
    auto hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swapchain,&device,&level,&context);
    if(FAILED(hr))hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swapchain,&device,&level,&context);
    return SUCCEEDED(hr)&&createTarget();
}
void saveConfig(const std::wstring& path,const Config& config){
    auto c=config;sanitize(c);std::ofstream f(std::filesystem::path(path),std::ios::binary|std::ios::trunc);
    unsigned version=kVersion;f.write(reinterpret_cast<const char*>(&version),sizeof(version));f.write(reinterpret_cast<const char*>(&c),sizeof(c));
}
void loadConfig(const std::wstring& path,Config& c){
    std::ifstream f(std::filesystem::path(path),std::ios::binary);unsigned v=0;Config loaded{};
    if(!f.read(reinterpret_cast<char*>(&v),sizeof(v)))return;
    const auto bytes=v==4?offsetof(Config,language):v==5?offsetof(Config,moveSpeed):v==kVersion?sizeof(Config):0;
    if(bytes&&f.read(reinterpret_cast<char*>(&loaded),bytes)){sanitize(loaded);c=loaded;}
}
ImU32 color(const float* c){return ImGui::ColorConvertFloat4ToU32({c[0],c[1],c[2],c[3]});}
const char* localized(int language,const char* pl,const char* en,const char* ru){return language==1?en:language==2?ru:pl;}
void shadowTextScaled(ImDrawList* dl,ImVec2 p,ImU32 c,const char* s,float scale){
    float size=ImGui::GetFontSize()*scale;auto font=ImGui::GetFont();
    dl->AddText(font,size,{p.x+1,p.y+1},IM_COL32(0,0,0,235),s);dl->AddText(font,size,p,c,s);
}
void drawBox(ImDrawList* dl,ImVec2 lo,ImVec2 hi,ImU32 tint,int style,float thickness,float rounding,bool outline,bool fill,float fillAlpha){
    if(hi.x-lo.x<3||hi.y-lo.y<3)return;
    if(fill)dl->AddRectFilled(lo,hi,(tint&0x00ffffff)|(static_cast<ImU32>(fillAlpha*255)<<24),rounding);
    auto line=[&](ImVec2 a,ImVec2 b){if(outline)dl->AddLine(a,b,IM_COL32(0,0,0,200),thickness+2);dl->AddLine(a,b,tint,thickness);};
    if(style==0){if(outline)dl->AddRect(lo,hi,IM_COL32(0,0,0,200),rounding,0,thickness+2);dl->AddRect(lo,hi,tint,rounding,0,thickness);}
    else if(style==1){float dx=std::clamp((hi.x-lo.x)*.24f,8.f,30.f),dy=std::clamp((hi.y-lo.y)*.20f,8.f,36.f);
        for(int corner=0;corner<4;++corner){ImVec2 p{corner&1?hi.x:lo.x,corner&2?hi.y:lo.y};
            line(p,{p.x+(corner&1?-dx:dx),p.y});line(p,{p.x,p.y+(corner&2?-dy:dy)});}}
    else {line({lo.x,lo.y},{hi.x,lo.y});line({lo.x,hi.y},{hi.x,hi.y});}
}
void offscreenArrow(ImDrawList* dl,ImVec2 target,float width,float height,float size,ImU32 tint){
    ImVec2 center{width*.5f,height*.5f};float dx=target.x-center.x,dy=target.y-center.y;
    float length=std::hypot(dx,dy);if(length<1)return;dx/=length;dy/=length;
    float edge=std::min((width*.5f-size*2)/std::max(std::abs(dx),.001f),(height*.5f-size*2)/std::max(std::abs(dy),.001f));
    ImVec2 p{center.x+dx*edge,center.y+dy*edge};ImVec2 tip{p.x+dx*size,p.y+dy*size};
    ImVec2 left{p.x-dy*size*.55f,p.y+dx*size*.55f},right{p.x+dy*size*.55f,p.y-dx*size*.55f};
    dl->AddTriangleFilled(tip,left,right,IM_COL32(0,0,0,220));dl->AddTriangle(tip,left,right,tint,2);
}
std::string keyLabel(int key){
    switch(key){case VK_LBUTTON:return "MOUSE1";case VK_RBUTTON:return "MOUSE2";case VK_MBUTTON:return "MOUSE3";case VK_XBUTTON1:return "MOUSE4";case VK_XBUTTON2:return "MOUSE5";case VK_LSHIFT:return "L SHIFT";case VK_RSHIFT:return "R SHIFT";}
    char buffer[64]{};GetKeyNameTextA(static_cast<LONG>(MapVirtualKeyA(key,MAPVK_VK_TO_VSC)<<16),buffer,sizeof(buffer));return buffer[0]?buffer:std::to_string(key);
}
void drawEsp(const Config& c,const Frame& f,float width,float height){
    if(f.width<=0||f.height<=0)return;float sx=width/f.width,sy=height/f.height;auto dl=ImGui::GetBackgroundDrawList();
    if(c.fovCircle&&c.enabled)dl->AddCircle({width*.5f,height*.5f},f.fovRadius*sy,IM_COL32(130,165,210,160),96,1);
    if(c.itemEsp)for(int i=0;i<f.itemCount&&i<kMaxItems;++i){const auto& item=f.items[i];
        if(item.distance>c.itemEspDistance||(item.kind==1&&!c.itemDungeon)||(item.kind==2&&!c.itemPortal)||(item.kind==3&&!c.itemMerchant)||(item.kind==4&&!c.itemChest))continue;
        const float* palette=item.kind==1?c.itemDungeonColor:item.kind==2?c.itemPortalColor:item.kind==3?c.itemMerchantColor:c.itemChestColor;
        ImU32 tint=color(palette);if(!item.visible)tint=(tint&0x00ffffff)|0xa0000000;
        ImVec2 p{item.x*sx,item.y*sy},lo{item.left*sx,item.top*sy},hi{item.right*sx,item.bottom*sy};
        bool onScreen=p.x>=0&&p.y>=0&&p.x<=width&&p.y<=height;
        if(!onScreen){if(c.espOffscreenArrows)offscreenArrow(dl,p,width,height,c.offscreenArrowSize,tint);continue;}
        if(item.hasBounds&&c.itemBoxes)drawBox(dl,lo,hi,tint,c.boxStyle,c.itemBoxThickness,c.boxRounding,c.espOutline,c.itemFill,c.itemFillAlpha);
        else {dl->AddCircleFilled(p,5,IM_COL32(0,0,0,220),16);dl->AddCircle(p,5,tint,16,2);}
        const char* label=item.kind==1?localized(c.language,"Dungeon","Dungeon","Подземелье"):item.kind==2?localized(c.language,"Portal","Portal","Портал"):item.kind==3?localized(c.language,"Kupiec","Merchant","Торговец"):localized(c.language,"Skrzynia","Chest","Сундук");
        if(c.itemNames)shadowTextScaled(dl,{item.hasBounds?lo.x:p.x+11,item.hasBounds?lo.y-20:p.y-8},tint,label,c.espTextScale);
        if(c.itemDistance){char caption[32];std::snprintf(caption,sizeof(caption),"%.0f m",item.distance);shadowTextScaled(dl,{item.hasBounds?lo.x:p.x+11,item.hasBounds?hi.y+3:p.y+10},tint,caption,c.espTextScale);}
        if(c.itemLines)dl->AddLine({width*.5f,height-4},{item.hasBounds?(lo.x+hi.x)*.5f:p.x,item.hasBounds?hi.y:p.y},tint,1);
    }
    if(!c.esp)return;
    for(int i=0;i<f.count&&i<kMaxEnemies;++i){auto& e=f.enemies[i];if(e.distance>c.espDistance||(c.espVisibleOnly&&!e.visible))continue;
        auto col=color(e.visible?c.visibleColor:c.hiddenColor);ImVec2 lo{e.left*sx,e.top*sy},hi{e.right*sx,e.bottom*sy};
        if(e.x*sx<0||e.x*sx>width||e.y*sy<0||e.y*sy>height){if(c.espOffscreenArrows)offscreenArrow(dl,{e.x*sx,e.y*sy},width,height,c.offscreenArrowSize,col);continue;}
        if(c.boxes&&e.hasBounds)drawBox(dl,lo,hi,col,c.boxStyle,c.boxThickness,c.boxRounding,c.espOutline,c.boxFill,c.fillAlpha);
        if(c.names)shadowTextScaled(dl,{lo.x,c.namePosition?hi.y+3:lo.y-20},col,e.name,c.espTextScale);
        if(c.distance){char buf[32];std::snprintf(buf,sizeof(buf),"%.1f m",e.distance);shadowTextScaled(dl,{lo.x,c.namePosition?lo.y-20:hi.y+3},col,buf,c.espTextScale);}
        if(c.lines)dl->AddLine({width*.5f,c.lineOrigin==0?4.f:c.lineOrigin==1?height*.5f:height-4},{(lo.x+hi.x)*.5f,hi.y},col,1);
        if(c.weakPoint&&e.hasWeakspot)dl->AddCircle({e.weakX*sx,e.weakY*sy},e.selected?5.f:3.f,col,16,e.selected?2.f:1.f);
        if(c.espCenterDot&&e.hasBounds)dl->AddCircleFilled({(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f},3,col,12);
        if(c.espHeadDot&&e.hasBounds)dl->AddCircleFilled({(lo.x+hi.x)*.5f,lo.y+6},3,col,12);
        if(c.espSelectedHighlight&&e.selected&&e.hasBounds)dl->AddRect({lo.x-3,lo.y-3},{hi.x+3,hi.y+3},IM_COL32(255,255,255,180),c.boxRounding,0,1);
        if(e.hasBounds){float x=lo.x-7;auto bar=[&](bool enabled,float ratio,ImU32 tint){if(!enabled||ratio<0)return;
            dl->AddRectFilled({x-1,lo.y-1},{x+4,hi.y+1},IM_COL32(0,0,0,210));
            dl->AddRectFilled({x,hi.y-(hi.y-lo.y)*ratio},{x+3,hi.y},tint);x-=6;};
            bar(c.healthBar,e.health,IM_COL32(102,216,144,255));bar(c.shieldBar,e.shield,IM_COL32(90,186,245,255));bar(c.armorBar,e.armor,IM_COL32(244,185,70,255));}
    }
}
void drawAimPreview(Config& c){
    auto T=[&](const char* pl,const char* en,const char* ru){return localized(c.language,pl,en,ru);};
    ImGui::TextDisabled("%s",T("Podglad punktu celowania (kliknij sylwetke)","Aim point preview (click the model)","Предпросмотр точки прицеливания (нажмите на модель)"));
    ImVec2 origin=ImGui::GetCursorScreenPos();ImGui::InvisibleButton("##aim-preview",{230,185});
    auto dl=ImGui::GetWindowDrawList();ImVec2 center{origin.x+115,origin.y+10};
    dl->AddRectFilled(origin,{origin.x+230,origin.y+185},IM_COL32(17,18,27,255),8);
    ImU32 body=IM_COL32(87,82,110,255),accent=IM_COL32(189,146,239,255);
    dl->AddCircleFilled({center.x,center.y+34},18,body,24);
    dl->AddRectFilled({center.x-20,center.y+54},{center.x+20,center.y+113},body,9);
    dl->AddLine({center.x-17,center.y+62},{center.x-42,center.y+112},body,10);
    dl->AddLine({center.x+17,center.y+62},{center.x+42,center.y+112},body,10);
    dl->AddLine({center.x-10,center.y+112},{center.x-17,center.y+166},body,11);
    dl->AddLine({center.x+10,center.y+112},{center.x+17,center.y+166},body,11);
    ImVec2 points[4]={{center.x+8,center.y+29},{center.x,center.y+91},{center.x,center.y+29},{center.x,center.y+42}};
    if(ImGui::IsItemHovered()&&ImGui::IsMouseClicked(0)){
        auto mouse=ImGui::GetIO().MousePos;float nearest=20*20;int chosen=-1;
        for(int i=0;i<4;++i){float dx=mouse.x-points[i].x,dy=mouse.y-points[i].y,d=dx*dx+dy*dy;if(d<nearest){nearest=d;chosen=i;}}
        if(chosen>=0)c.bone=chosen;
    }
    for(int i=0;i<4;++i){auto tint=i==c.bone?accent:IM_COL32(173,170,193,150);dl->AddCircle(points[i],i==c.bone?8:5,tint,24,i==c.bone?2:1);}
    ImGui::TextDisabled("%s",c.bone==0?T("Najblizszy weakspot","Nearest weak spot","Ближайшая уязвимая точка"):c.bone==1?T("Srodek ciala","Body center","Центр тела"):c.bone==2?T("Weakspot glowy","Head weak spot","Уязвимая точка головы"):T("Srodek glowy","Head center","Центр головы"));
}
void drawEspPreview(const Config& c){
    ImGui::TextDisabled("%s",localized(c.language,"PODGLAD ESP","ESP PREVIEW","ПРЕДПРОСМОТР ESP"));ImVec2 p=ImGui::GetCursorScreenPos();ImGui::InvisibleButton("##esp-preview",{260,320});
    auto dl=ImGui::GetWindowDrawList();auto col=color(c.visibleColor);
    dl->AddRectFilled(p,{p.x+260,p.y+320},IM_COL32(17,18,26,255),8);
    ImVec2 lo{p.x+76,p.y+50},hi{p.x+184,p.y+239};
    dl->AddCircleFilled({p.x+130,p.y+95},15,IM_COL32(89,84,103,255),24);
    dl->AddRectFilled({p.x+115,p.y+112},{p.x+145,p.y+177},IM_COL32(89,84,103,255),9);
    dl->AddLine({p.x+120,p.y+174},{p.x+112,p.y+221},IM_COL32(89,84,103,255),10);
    dl->AddLine({p.x+140,p.y+174},{p.x+148,p.y+221},IM_COL32(89,84,103,255),10);
    if(c.boxes)drawBox(dl,lo,hi,col,c.boxStyle,c.boxThickness,c.boxRounding,c.espOutline,c.boxFill,c.fillAlpha);
    if(c.names)shadowTextScaled(dl,{lo.x,c.namePosition?hi.y+4:lo.y-20},col,"Shielded Crossbowman",c.espTextScale);
    if(c.distance)shadowTextScaled(dl,{lo.x,c.namePosition?lo.y-20:hi.y+5},col,"18.4 m",c.espTextScale);
    if(c.healthBar){dl->AddRectFilled({lo.x-9,lo.y},{lo.x-5,hi.y},IM_COL32(35,35,39,255));dl->AddRectFilled({lo.x-9,lo.y+55},{lo.x-5,hi.y},IM_COL32(102,216,144,255));}
    if(c.shieldBar){dl->AddRectFilled({lo.x-15,lo.y+95},{lo.x-12,hi.y},IM_COL32(90,186,245,255));}
    if(c.weakPoint)dl->AddCircle({p.x+130,p.y+90},4,col,16,2);
    if(c.espCenterDot)dl->AddCircleFilled({p.x+130,p.y+145},3,col,12);
    if(c.espHeadDot)dl->AddCircleFilled({p.x+130,p.y+58},3,col,12);
    if(c.lines)dl->AddLine({p.x+130,p.y+310},{p.x+130,hi.y},col,1);
}
void menu(Config& c,const Frame& f,const std::wstring& path){
    auto T=[&](const char* pl,const char* en,const char* ru){return localized(c.language,pl,en,ru);};
    ImGui::SetNextWindowSize({860,850},ImGuiCond_FirstUseEver);ImGui::SetNextWindowPos({40,24},ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("SCARLET / ASSIST",&c.menuOpen,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoTitleBar)){ImGui::End();return;}
    ImGui::TextColored({.72f,.55f,.92f,1},"S C A R L E T  /  A S S I S T");ImGui::SameLine(610);
    if(ImGui::SmallButton(T("Ukryj [Ins]","Hide [Ins]","Скрыть [Ins]")))c.menuOpen=false;
    ImGui::SameLine(715);ImGui::SetNextItemWidth(90);ImGui::Combo("##language",&c.language,"PL\0EN\0RU\0");
    ImGui::TextDisabled("%s",T("Autor: haszKEJL  |  Wersja: 1.0.0","Author: haszKEJL  |  Version: 1.0.0","Автор: haszKEJL  |  Версия: 1.0.0"));
    ImGui::TextDisabled("GUNFIRE REBORN");ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
    if(ImGui::BeginTabBar("tabs")){
        if(ImGui::BeginTabItem(T("Aimbot###tab-aimbot","Aimbot###tab-aimbot","Аимбот###tab-aimbot"))){
            ImGui::Checkbox(T("Wlaczony","Enabled","Включено"),&c.enabled);ImGui::SameLine();ImGui::Checkbox(T("Na klawisz","On key","По клавише"),&c.onKey);ImGui::SameLine();
            static bool capture=false;static ULONGLONG captureAt=0;
            if(ImGui::Button(capture?T("Nacisnij klawisz...","Press a key...","Нажмите клавишу..."):keyLabel(c.key).c_str(),{145,0})){capture=true;captureAt=GetTickCount64();}
            if(capture&&GetTickCount64()-captureAt>250){for(int k=1;k<255;++k)if(k!=VK_INSERT&&k!=VK_END&&k!=VK_SHIFT&&k!=VK_CONTROL&&k!=VK_MENU&&(GetAsyncKeyState(k)&0x8000)){c.key=k;capture=false;break;}}
            ImGui::SameLine();ImGui::SetNextItemWidth(110);ImGui::Combo("##mode",&c.keyMode,T("Przytrzymaj\0Przelacz\0Zawsze\0","Hold\0Toggle\0Always\0","Удерживать\0Переключить\0Всегда\0"));
            ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
            ImGui::TextUnformatted(T("Profil: wszystkie bronie","Profile: all weapons","Профиль: все оружие"));
            if(ImGui::BeginTable("aim-grid",2,ImGuiTableFlags_SizingStretchSame)){
                ImGui::TableNextColumn();ImGui::Checkbox(T("Blokada celu","Aimlock","Фиксация цели"),&c.aimlock);ImGui::Checkbox(T("Silent","Silent","Тихий аим"),&c.silent);
                ImGui::Checkbox(T("Tylko widoczni","Visible only","Только видимые"),&c.visibleOnly);ImGui::Checkbox(T("Tylko z luneta","Scoped only","Только с прицелом"),&c.scopedOnly);
                ImGui::Spacing();ImGui::TextUnformatted(T("Punkt celowania","Aim point","Точка прицеливания"));ImGui::SetNextItemWidth(-1);ImGui::Combo("##bone",&c.bone,T("Najblizszy weakspot\0Srodek ciala\0Weakspot glowy\0Srodek glowy\0","Nearest weak spot\0Body center\0Head weak spot\0Head center\0","Ближайшая уязвимая точка\0Центр тела\0Уязвимая точка головы\0Центр головы\0"));
                ImGui::Checkbox(T("Tylko prawdziwy weakspot","Require real weak spot","Только реальная уязвимая точка"),&c.strictWeakspot);
                ImGui::TextUnformatted(T("Priorytet celu","Target priority","Приоритет цели"));ImGui::SetNextItemWidth(-1);ImGui::Combo("##priority",&c.priority,T("Blisko celownika\0Najblizszy przeciwnik\0","Closest to crosshair\0Nearest enemy\0","Ближе к прицелу\0Ближайший враг\0"));
                ImGui::TableNextColumn();ImGui::TextUnformatted("FOV");ImGui::SetNextItemWidth(-1);ImGui::SliderFloat("##FOV",&c.fov,1,179,"%.1f deg");
                ImGui::TextUnformatted(T("Wygladzanie","Smoothing","Сглаживание"));ImGui::SetNextItemWidth(-1);ImGui::SliderFloat("##Smooth",&c.smooth,1,50,"%.1f");
                ImGui::TextUnformatted(T("Maks. dystans","Maximum distance","Макс. расстояние"));ImGui::SetNextItemWidth(-1);ImGui::SliderFloat("##distance",&c.maxDistance,5,500,"%.0f m");
                ImGui::Checkbox(T("Pokaz okrag FOV","Show FOV circle","Показать круг FOV"),&c.fovCircle);drawAimPreview(c);ImGui::EndTable();
            }
            ImGui::Spacing();
            ImGui::TextDisabled("%s",T("Dodatkowe ustawienia ruchu: zakladka Legit.","Additional movement settings: Legit tab.","Дополнительные настройки движения: вкладка Legit."));
            if(c.silent)ImGui::TextWrapped(T("Silent: tory %d/3 | promien %s | korekty %u | celowanie %u/%u","Silent: paths %d/3 | ray %s | redirects %u | aim %u/%u","Silent: траектории %d/3 | луч %s | коррекции %u | прицел %u/%u"),f.silentFamilies,f.silentRayReady?"OK":"...",f.silentRedirects,f.aimPositionRedirects,f.aimPositionCalls);
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Legit###tab-legit",nullptr,std::strstr(GetCommandLineA(),"--preview-legit")?ImGuiTabItemFlags_SetSelected:0)){
            ImGui::TextDisabled("%s",T("Ruch celownika i wybor celu","Crosshair movement and target selection","Движение прицела и выбор цели"));
            ImGui::Checkbox(T("Tylko podczas strzelania","Only while firing","Только при стрельбе"),&c.legitOnlyFiring);
            auto slider=[&](const char* id,const char* label,float* value,float min,float max,const char* format){
                ImGui::PushID(id);ImGui::TextUnformatted(label);ImGui::SetNextItemWidth(-1);ImGui::SliderFloat("##value",value,min,max,format);ImGui::PopID();};
            if(ImGui::BeginTable("legit-grid",2,ImGuiTableFlags_SizingStretchSame)){
                ImGui::TableNextColumn();
                slider("deadzone",T("Martwa strefa","Dead zone","Мёртвая зона"),&c.deadzone,0,30,"%.1f px");
                slider("speed",T("Limit predkosci","Speed limit","Ограничение скорости"),&c.aimSpeed,50,6000,"%.0f px/s");
                slider("reaction",T("Czas reakcji","Reaction delay","Задержка реакции"),&c.reactionMs,0,1000,"%.0f ms");
                slider("reactionvar",T("Zmiennosc reakcji","Reaction variation","Разброс реакции"),&c.reactionJitterMs,0,250,"%.0f ms");
                slider("switch",T("Zmiana celu","Target switch delay","Смена цели"),&c.switchMs,0,1000,"%.0f ms");
                slider("stick",T("Przywiazanie do celu","Target stickiness","Удержание цели"),&c.legitStickiness,0,100,"%.0f %%");
                ImGui::TableNextColumn();
                slider("horizontal",T("Predkosc pozioma","Horizontal strength","Горизонтальная скорость"),&c.legitHorizontal,.1f,2,"%.2f x");
                slider("vertical",T("Predkosc pionowa","Vertical strength","Вертикальная скорость"),&c.legitVertical,.1f,2,"%.2f x");
                slider("acceleration",T("Rozpedzanie","Acceleration time","Время разгона"),&c.legitAccelerationMs,0,1000,"%.0f ms");
                slider("curve",T("Zwalnianie przy celu","Slowdown near target","Замедление у цели"),&c.legitCurve,0,2,"%.2f");
                slider("variation",T("Zmiennosc predkosci","Speed variation","Изменение скорости"),&c.legitVariation,0,.25f,"%.2f");
                ImGui::EndTable();
            }
            ImGui::TextWrapped("%s",T("Ustawienia Legit dzialaja przy zwyklym ruchu celownika. Silent korzysta z osobnej korekty toru.","Legit settings shape normal crosshair movement. Silent uses a separate projectile correction.","Настройки Legit управляют движением прицела. Silent использует отдельную коррекцию траектории."));
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("ESP###tab-esp",nullptr,std::strstr(GetCommandLineA(),"--preview-esp")?ImGuiTabItemFlags_SetSelected:0)){
            ImGui::BeginChild("esp-scroll",{0,550},ImGuiChildFlags_None);
            ImGui::Checkbox(T("ESP przeciwnikow","Enemy ESP","ESP врагов"),&c.esp);ImGui::Separator();
            if(ImGui::BeginTable("esp-layout",2,ImGuiTableFlags_SizingStretchSame)){
                ImGui::TableNextColumn();
                ImGui::Checkbox(T("Ramki","Boxes","Рамки"),&c.boxes);ImGui::Checkbox(T("Nazwy","Names","Имена"),&c.names);
                ImGui::Checkbox(T("Odleglosc","Distance","Расстояние"),&c.distance);ImGui::Checkbox(T("Linie","Snaplines","Линии"),&c.lines);
                ImGui::Checkbox(T("Weakspot","Weak spot","Уязвимая точка"),&c.weakPoint);ImGui::Checkbox(T("Tylko widoczni","Visible only","Только видимые"),&c.espVisibleOnly);
                ImGui::Checkbox("HP",&c.healthBar);ImGui::Checkbox(T("Tarcza","Shield","Щит"),&c.shieldBar);ImGui::Checkbox(T("Pancerz","Armor","Броня"),&c.armorBar);
                ImGui::Checkbox(T("Dokladne granice","Precise bounds","Точные границы"),&c.preciseBoxes);
                ImGui::Checkbox(T("Czarna obwodka","Black outline","Чёрная обводка"),&c.espOutline);
                ImGui::Checkbox(T("Podswietl wybrany cel","Highlight selected target","Выделять выбранную цель"),&c.espSelectedHighlight);
                ImGui::Checkbox(T("Punkt srodka","Center dot","Точка в центре"),&c.espCenterDot);
                ImGui::Checkbox(T("Punkt glowy","Head dot","Точка головы"),&c.espHeadDot);
                ImGui::Checkbox(T("Strzalki poza ekranem","Offscreen arrows","Стрелки вне экрана"),&c.espOffscreenArrows);
                ImGui::TableNextColumn();
                drawEspPreview(c);
                ImGui::TextUnformatted(T("Styl ramki","Box style","Стиль рамки"));ImGui::SetNextItemWidth(-1);ImGui::Combo("##boxstyle",&c.boxStyle,T("Pelna\0Narozniki\0Poziome\0","Full\0Corners\0Horizontal\0","Полная\0Углы\0Горизонтальная\0"));
                ImGui::TextUnformatted(T("Polozenie nazwy","Name position","Положение имени"));ImGui::SetNextItemWidth(-1);ImGui::Combo("##namepos",&c.namePosition,T("Gora\0Dol\0","Top\0Bottom\0","Сверху\0Снизу\0"));
                ImGui::SliderFloat(T("Grubosc ramki","Box thickness","Толщина рамки"),&c.boxThickness,.5f,4,"%.1f px");
                ImGui::SliderFloat(T("Zaokraglenie","Corner rounding","Скругление"),&c.boxRounding,0,16,"%.0f px");
                ImGui::SliderFloat(T("Rozmiar tekstu","Text size","Размер текста"),&c.espTextScale,.7f,1.8f,"%.2f x");
                ImGui::Checkbox(T("Wypelnienie","Fill","Заливка"),&c.boxFill);ImGui::SliderFloat(T("Krycie wypelnienia","Fill opacity","Непрозрачность заливки"),&c.fillAlpha,0,.5f,"%.2f");
                ImGui::TextUnformatted(T("Poczatek linii","Line origin","Начало линии"));ImGui::SetNextItemWidth(-1);ImGui::Combo("##lineorigin",&c.lineOrigin,T("Gora\0Celownik\0Dol\0","Top\0Crosshair\0Bottom\0","Сверху\0Прицел\0Снизу\0"));
                ImGui::SliderFloat(T("Zasieg ESP","ESP range","Дальность ESP"),&c.espDistance,5,500,"%.0f m");
                ImGui::SliderFloat(T("Rozmiar strzalki","Arrow size","Размер стрелки"),&c.offscreenArrowSize,5,30,"%.0f px");
                ImGui::ColorEdit4(T("Widoczny","Visible","Видимый"),c.visibleColor);
                ImGui::ColorEdit4(T("Za przeszkoda","Behind cover","За укрытием"),c.hiddenColor);
                ImGui::EndTable();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem(T("Triggerbot###tab-trigger","Triggerbot###tab-trigger","Триггербот###tab-trigger"))){
            ImGui::Checkbox(T("Wlacz triggerbot","Enable triggerbot","Включить triggerbot"),&c.triggerEnabled);ImGui::Checkbox(T("Tylko na klawisz","Only on key","Только по клавише"),&c.triggerOnKey);
            static bool triggerCapture=false;static ULONGLONG triggerCaptureAt=0;
            if(ImGui::Button(triggerCapture?T("Nacisnij klawisz...","Press a key...","Нажмите клавишу..."):keyLabel(c.triggerKey).c_str(),{145,0})){triggerCapture=true;triggerCaptureAt=GetTickCount64();}
            ImGui::SameLine();ImGui::TextDisabled("%s",T("Klawisz triggerbota","Trigger key","Клавиша triggerbot"));
            if(triggerCapture&&GetTickCount64()-triggerCaptureAt>250){for(int k=1;k<255;++k)if(k!=VK_INSERT&&k!=VK_END&&k!=VK_SHIFT&&k!=VK_CONTROL&&k!=VK_MENU&&(GetAsyncKeyState(k)&0x8000)){c.triggerKey=k;triggerCapture=false;break;}}
            ImGui::Checkbox(T("Tylko widoczni","Visible only","Только видимые"),&c.triggerVisibleOnly);ImGui::Checkbox(T("Tylko weakspot","Weak spot only","Только уязвимая точка"),&c.triggerWeakspot);
            ImGui::SliderFloat(T("Opoznienie","Delay","Задержка"),&c.triggerDelayMs,0,500,"%.0f ms");
            ImGui::SliderFloat(T("Odstep strzalow","Shot interval","Интервал выстрелов"),&c.triggerCooldownMs,50,1000,"%.0f ms");
            ImGui::SliderFloat(T("Zasieg","Range","Дальность"),&c.triggerDistance,5,500,"%.0f m");
            ImGui::TextWrapped("%s",T("Oddaje pojedynczy strzal, kiedy celownik znajduje sie na modelu.","Fires once when the crosshair is on a target model.","Стреляет один раз, когда прицел находится на модели цели."));
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem(T("Walka###tab-combat","Combat###tab-combat","Бой###tab-combat"))){
            ImGui::Checkbox(T("Bez rozrzutu","No spread","Без разброса"),&c.noSpread);
            ImGui::TextDisabled("%s",T("Usuwa rozrzut broni.","Removes weapon spread.","Убирает разброс оружия."));
            ImGui::Separator();ImGui::Checkbox(T("Bez odrzutu","No recoil","Без отдачи"),&c.noRecoil);
            ImGui::TextDisabled("%s",T("Wylacza odrzut kamery i broni.","Disables camera and weapon recoil.","Отключает отдачу камеры и оружия."));
            ImGui::Separator();ImGui::Checkbox(T("Trafienia w weakspot","Weakness Hit","Уязвимые попадания"),&c.weaknessHit);
            ImGui::TextDisabled("%s",T("Oznacza trafienia potworow jako trafienia w weakspot.","Marks monster hits as weak spot hits.","Помечает попадания по монстрам как уязвимые."));
            ImGui::Separator();
            ImGui::SliderFloat(T("Szybkosc postaci","Movement speed","Скорость движения"),&c.moveSpeed,1,3,"%.2f x");
            ImGui::SliderFloat(T("Szybkosc przeladowania","Reload speed","Скорость перезарядки"),&c.reloadSpeed,1,3,"%.2f x");
            ImGui::SliderFloat(T("Szybkosc strzelania","Fire rate","Скорострельность"),&c.fireRate,1,3,"%.2f x");
            ImGui::TextWrapped("%s",T("Dzialanie moze zalezec od hosta.","Effects may depend on the host.","Эффект может зависеть от хоста."));
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem(T("Swiat###tab-world","World###tab-world","Мир###tab-world"),nullptr,std::strstr(GetCommandLineA(),"--preview-world")?ImGuiTabItemFlags_SetSelected:0)){
            ImGui::Checkbox(T("ESP obiektow","Item ESP","ESP объектов"),&c.itemEsp);ImGui::Separator();
            ImGui::Checkbox(T("Dungeon","Dungeon","Подземелье"),&c.itemDungeon);ImGui::SameLine(210);ImGui::ColorEdit4("##dungeon-color",c.itemDungeonColor,ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel);
            ImGui::Checkbox(T("Portal","Portal","Портал"),&c.itemPortal);ImGui::SameLine(210);ImGui::ColorEdit4("##portal-color",c.itemPortalColor,ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel);
            ImGui::Checkbox(T("Kupiec","Merchant","Торговец"),&c.itemMerchant);ImGui::SameLine(210);ImGui::ColorEdit4("##merchant-color",c.itemMerchantColor,ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel);
            ImGui::Checkbox(T("Skrzynia","Chest","Сундук"),&c.itemChest);ImGui::SameLine(210);ImGui::ColorEdit4("##chest-color",c.itemChestColor,ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel);
            ImGui::Separator();
            ImGui::Checkbox(T("Ramki obiektow","Object boxes","Рамки объектов"),&c.itemBoxes);ImGui::SameLine(300);ImGui::Checkbox(T("Nazwy obiektow","Object names","Названия объектов"),&c.itemNames);
            ImGui::Checkbox(T("Odleglosc obiektow","Object distance","Расстояние до объектов"),&c.itemDistance);ImGui::SameLine(300);ImGui::Checkbox(T("Linie obiektow","Object lines","Линии объектов"),&c.itemLines);
            ImGui::Checkbox(T("Wypelnienie ramek","Box fill","Заливка рамок"),&c.itemFill);
            ImGui::SliderFloat(T("Grubosc ramek","Box thickness","Толщина рамок"),&c.itemBoxThickness,.5f,4,"%.1f px");
            ImGui::SliderFloat(T("Krycie wypelnienia##item","Fill opacity##item","Непрозрачность##item"),&c.itemFillAlpha,0,.5f,"%.2f");
            ImGui::SliderFloat(T("Zasieg Item ESP","Item ESP range","Дальность Item ESP"),&c.itemEspDistance,5,500,"%.0f m");
            ImGui::TextDisabled("%s",T("Ramki dopasowuja sie do modeli obiektow.","Boxes follow object models.","Рамки соответствуют моделям объектов."));
            ImGui::Separator();ImGui::Checkbox(T("Auto zbieranie","Auto Pickup","Автоподбор"),&c.autoPickup);
            ImGui::TextWrapped("%s",T("Przenosi lezace przedmioty do postaci co 0.5 s.","Moves dropped items to the player every 0.5 s.","Перемещает выпавшие предметы к игроку каждые 0.5 с."));
            ImGui::TextDisabled(T("Przedmioty: %d | obiekty: %d | przeniesienia: %d","Drops: %d | objects: %d | moves: %d","Предметы: %d | объекты: %d | переносы: %d"),f.nearbyDrops,f.itemCount,f.pickupMoves);
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem(T("GlowESP###tab-glow","GlowESP###tab-glow","Свечение ESP###tab-glow"))){
            ImGui::Checkbox(T("Glow modeli","Model glow","Свечение моделей"),&c.glow);ImGui::Checkbox(T("Przez sciany","Through walls","Сквозь стены"),&c.glowThroughWalls);
            ImGui::SliderFloat(T("Intensywnosc","Intensity","Интенсивность"),&c.glowStrength,.1f,3,"%.1f");
            ImGui::SliderFloat(T("Zasieg Glow","Glow range","Дальность свечения"),&c.glowDistance,5,500,"%.0f m");
            ImGui::ColorEdit4(T("Kolor i krycie modelu","Model color and opacity","Цвет и прозрачность модели"),c.glowColor);
            ImGui::TextWrapped("%s",T("Podswietlenie modeli dziala niezaleznie od ESP.","Model glow works independently of ESP.","Свечение моделей работает отдельно от ESP."));
            ImGui::Text(T("Renderowanie: %s","Renderer: %s","Отрисовка: %s"),f.glowReady?T("dostepne","available","доступно"):T("niedostepne","unavailable","недоступно"));
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem(T("Ustawienia###tab-settings","Settings###tab-settings","Настройки###tab-settings"))){
            ImGui::TextUnformatted(T("Jezyk interfejsu","Interface language","Язык интерфейса"));ImGui::SetNextItemWidth(130);ImGui::Combo("##settings-language",&c.language,"PL\0EN\0RU\0");
            if(ImGui::Button(T("Zapisz","Save","Сохранить")))saveConfig(path,c);ImGui::SameLine();
            if(ImGui::Button(T("Wczytaj","Load","Загрузить"))){loadConfig(path,c);c.menuOpen=true;}ImGui::SameLine();
            if(ImGui::Button(T("Domyslne","Defaults","По умолчанию")))c=Config{};
            ImGui::TextWrapped("%s",T("Insert: menu | End: zamknij. Ustawienia zapisuja sie przy zamknieciu. Gra musi dzialac w oknie lub borderless.","Insert: menu | End: quit. Settings save on exit. Use windowed or borderless mode.","Insert: меню | End: выход. Настройки сохраняются при выходе. Используйте оконный режим."));
            ImGui::TextWrapped("%s",T("Otwarte menu wstrzymuje celowanie.","An open menu pauses aiming.","Открытое меню останавливает прицеливание."));ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY()+8,ImGui::GetWindowHeight()-103));
    if(ImGui::Button(T("Wylacz wszystko [F9]","Disable all [F9]","Выключить всё [F9]"))){c.enabled=false;c.silent=false;c.esp=false;c.glow=false;c.triggerEnabled=false;c.noSpread=false;c.noRecoil=false;c.weaknessHit=false;c.autoPickup=false;c.itemEsp=false;c.moveSpeed=c.reloadSpeed=c.fireRate=1;}
    ImGui::SameLine();ImGui::TextDisabled("%s",T("Insert: menu. Otwarte menu wstrzymuje celowanie.","Insert: menu. An open menu pauses aiming.","Insert: меню. Открытое меню останавливает прицеливание."));ImGui::Separator();
    if(f.ready)ImGui::TextColored({.3f,.9f,.6f,1},T("Cele: %d | Obiekty: %d","Targets: %d | Objects: %d","Цели: %d | Объекты: %d"),f.count,f.itemCount);
    else ImGui::TextColored({1,.65f,.3f,1},"%s",T("Oczekiwanie na gre...","Waiting for game...","Ожидание игры..."));
    ImGui::TextDisabled("%s",T("Insert - menu    End - zamknij    C++ / Dear ImGui","Insert - menu    End - quit    C++ / Dear ImGui","Insert - меню    End - выход    C++ / Dear ImGui"));ImGui::End();
}
bool captureRender(const wchar_t* file){
    ID3D11Texture2D* source=nullptr;if(FAILED(swapchain->GetBuffer(0,IID_PPV_ARGS(&source))))return false;
    D3D11_TEXTURE2D_DESC d;source->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
    ID3D11Texture2D* staging=nullptr;if(FAILED(device->CreateTexture2D(&d,nullptr,&staging))){source->Release();return false;}
    context->CopyResource(staging,source);D3D11_MAPPED_SUBRESOURCE map{};bool ok=false;
    if(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&map))){std::ofstream out(std::filesystem::path(file),std::ios::binary);out<<"P6\n"<<d.Width<<" "<<d.Height<<"\n255\n";
        for(unsigned y=0;y<d.Height;++y){auto row=static_cast<unsigned char*>(map.pData)+y*map.RowPitch;for(unsigned x=0;x<d.Width;++x)out.write(reinterpret_cast<char*>(row+x*4),3);}ok=out.good();context->Unmap(staging,0);}
    staging->Release();source->Release();return ok;
}
}
int runOverlay(HWND game,DWORD gameThread,Shared* shared,HANDLE mutex,HANDLE process,const std::wstring& path,bool preview,bool manual){
    quit=false;ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc{sizeof(wc),CS_CLASSDC,windowProc,0,0,GetModuleHandleW(nullptr),nullptr,LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)),nullptr,nullptr,L"ScarletAimOverlay",nullptr};
    RegisterClassExW(&wc);auto w=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED,wc.lpszClassName,L"ScarletAim",WS_POPUP,0,0,1280,900,nullptr,nullptr,wc.hInstance,nullptr);
    if(!w||!createD3D(w)){MessageBoxW(nullptr,L"Nie mozna utworzyc nakladki DirectX 11.",L"ScarletAim",MB_ICONERROR);return 1;}
    SetLayeredWindowAttributes(w,0,255,LWA_ALPHA);MARGINS margins{-1,-1,-1,-1};DwmExtendFrameIntoClientArea(w,&margins);
    IMGUI_CHECKVERSION();ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    if(std::filesystem::exists(L"C:/Windows/Fonts/segoeui.ttf"))io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",16,nullptr,io.Fonts->GetGlyphRangesCyrillic());
    ImGui::StyleColorsDark();auto& style=ImGui::GetStyle();style.WindowRounding=14;style.ChildRounding=12;style.FrameRounding=7;style.WindowPadding={23,20};style.ItemSpacing={10,8};style.FramePadding={9,5};
    style.Colors[ImGuiCol_WindowBg]={.043f,.043f,.067f,.99f};style.Colors[ImGuiCol_ChildBg]={.075f,.075f,.11f,1};
    style.Colors[ImGuiCol_Text]={.83f,.82f,.89f,1};style.Colors[ImGuiCol_TextDisabled]={.47f,.49f,.59f,1};
    style.Colors[ImGuiCol_FrameBg]={.13f,.12f,.2f,1};style.Colors[ImGuiCol_FrameBgHovered]={.23f,.19f,.34f,1};style.Colors[ImGuiCol_FrameBgActive]={.28f,.22f,.4f,1};
    for(auto id:{ImGuiCol_Button,ImGuiCol_Header,ImGuiCol_Tab})style.Colors[id]={.19f,.15f,.29f,1};
    for(auto id:{ImGuiCol_ButtonHovered,ImGuiCol_HeaderHovered,ImGuiCol_TabHovered})style.Colors[id]={.29f,.23f,.42f,1};
    for(auto id:{ImGuiCol_ButtonActive,ImGuiCol_HeaderActive,ImGuiCol_TabSelected})style.Colors[id]={.34f,.26f,.47f,1};
    style.Colors[ImGuiCol_CheckMark]={.73f,.57f,.94f,1};style.Colors[ImGuiCol_SliderGrab]={.61f,.46f,.83f,1};style.Colors[ImGuiCol_SliderGrabActive]={.78f,.63f,1,1};
    ImGui_ImplWin32_Init(w);ImGui_ImplDX11_Init(device,context);ImGui_ImplWin32_EnableAlphaCompositing(w);
    Config config{};if(!preview)loadConfig(path,config);config.menuOpen=true;
    if(preview){config.itemEsp=true;const char* command=GetCommandLineA();if(std::strstr(command,"--preview-en"))config.language=1;else if(std::strstr(command,"--preview-ru"))config.language=2;}
    Frame f{};bool previousMenu=false,insertDown=false;unsigned iterations=0;auto message=RegisterWindowMessageW(kMessage);
    if(!preview)ShowWindow(w,SW_SHOWNORMAL);
    while(!quit){
        MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT)quit=true;TranslateMessage(&msg);DispatchMessageW(&msg);}if(quit)break;
        if(!preview&&((GetAsyncKeyState(VK_END)&0x8000)||(process&&WaitForSingleObject(process,0)==WAIT_OBJECT_0)))break;
        bool insert=(GetAsyncKeyState(VK_INSERT)&0x8000)!=0;if(insert&&!insertDown)config.menuOpen=!config.menuOpen;insertDown=insert;
        if(GetAsyncKeyState(VK_F9)&0x8000){config.enabled=false;config.silent=false;config.esp=false;config.glow=false;config.triggerEnabled=false;config.noSpread=false;config.noRecoil=false;config.weaknessHit=false;config.autoPickup=false;config.itemEsp=false;config.moveSpeed=config.reloadSpeed=config.fireRate=1;}
        RECT rect{0,0,1280,900};POINT corner{};
        if(game){if(!IsWindow(game))break;GetClientRect(game,&rect);ClientToScreen(game,&corner);}
        if(rect.right<=0||rect.bottom<=0){Sleep(50);continue;}
        auto foregroundWindow=GetForegroundWindow();bool show=preview||foregroundWindow==game||foregroundWindow==w;
        if(!preview)SetWindowPos(w,HWND_TOPMOST,corner.x,corner.y,rect.right,rect.bottom,SWP_NOACTIVATE|(show?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
        if(config.menuOpen!=previousMenu){auto ex=GetWindowLongPtrW(w,GWL_EXSTYLE);
            SetWindowLongPtrW(w,GWL_EXSTYLE,config.menuOpen?(ex&~(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE)):(ex|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE));
            if(!preview){if(config.menuOpen){ClipCursor(nullptr);SetForegroundWindow(w);}else SetForegroundWindow(game);}previousMenu=config.menuOpen;
        }
        if(shared&&WaitForSingleObject(mutex,0)==WAIT_OBJECT_0){shared->config=config;f=shared->frame;ReleaseMutex(mutex);}
        if(gameThread&&!manual)PostThreadMessageW(gameThread,message,2,0);
        if(preview){f.ready=true;f.width=1280;f.height=900;f.fovRadius=125;f.count=1;f.glowReady=true;
            auto& e=f.enemies[0];e.left=850;e.right=980;e.top=240;e.bottom=560;e.x=915;e.y=270;e.distance=14;e.visible=true;e.selected=true;e.hasBounds=true;e.hasWeakspot=true;std::strcpy(e.name,"Shielded Crossbowman");
            f.itemCount=2;auto& shop=f.items[0];shop.x=1060;shop.y=430;shop.left=1010;shop.right=1110;shop.top=315;shop.bottom=525;shop.distance=12;shop.kind=3;shop.hasBounds=shop.visible=true;
            auto& chest=f.items[1];chest.x=1140;chest.y=670;chest.left=1100;chest.right=1180;chest.top=620;chest.bottom=720;chest.distance=20;chest.kind=4;chest.hasBounds=chest.visible=true;}
        else if(GetTickCount64()-f.time>500){f.count=0;f.ready=false;std::strcpy(f.status,"Oczekiwanie na aktualne dane gry...");}
        ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();
        if(show)drawEsp(config,f,static_cast<float>(rect.right),static_cast<float>(rect.bottom));
        if(config.menuOpen&&show)menu(config,f,path);
        ImGui::Render();float clear[4]{0,0,0,0};context->OMSetRenderTargets(1,&targetView,nullptr);context->ClearRenderTargetView(targetView,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if(preview&&iterations==3){const char* command=GetCommandLineA();
            captureRender(std::strstr(command,"--preview-world")?L"bin/menu-preview-world.ppm":std::strstr(command,"--preview-esp")?L"bin/menu-preview-esp.ppm":std::strstr(command,"--preview-legit")?L"bin/menu-preview-legit.ppm":config.language==1?L"bin/menu-preview-en.ppm":config.language==2?L"bin/menu-preview-ru.ppm":L"bin/menu-preview.ppm");break;}
        auto hr=swapchain->Present(1,0);if(FAILED(hr))break;++iterations;Sleep(1);
    }
    if(!preview)saveConfig(path,config);
    ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext();releaseTarget();
    if(swapchain)swapchain->Release();if(context)context->Release();if(device)device->Release();swapchain=nullptr;context=nullptr;device=nullptr;
    DestroyWindow(w);UnregisterClassW(wc.lpszClassName,wc.hInstance);return 0;
}
