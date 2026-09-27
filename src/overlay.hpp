#pragma once
#include "shared.hpp"
int runOverlay(HWND game,DWORD gameThread,Shared* shared,HANDLE mutex,HANDLE process,const std::wstring& configPath,bool preview,bool manual=false);
