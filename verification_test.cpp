// SPDX-License-Identifier: MIT
// Copyright (c) 2026 IKNLM
﻿#include "lj_overlay.cpp"
#include <cassert>
#include <limits>
static int chainKeys=0, deliveredKeys=0, postedActions=0, popupChecks=0, closeRequests=0;
static LRESULT CALLBACK Witness(int code, WPARAM wp, LPARAM lp) {
    if(code==HC_ACTION && (wp==WM_KEYDOWN || wp==WM_SYSKEYDOWN)) ++chainKeys;
    return CallNextHookEx(nullptr,code,wp,lp);
}
static LRESULT CALLBACK TestWnd(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_CLOSE) {++closeRequests;return 0;}
    if(msg==WM_KEYDOWN) ++deliveredKeys;
    if(msg==WM_LJ_KEY) ++postedActions;
    return WndProc(h,msg,wp,lp);
}
static void pump(int ms=120) {
    ULONGLONG end=GetTickCount64()+ms;
    do { MSG m{}; while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) {
        if(m.message!=WM_QUIT) {TranslateMessage(&m); DispatchMessageW(&m);}
    } Sleep(2); } while(GetTickCount64()<end);
}
static Jump make(const char* type,double dist,const char* mode) {
    Jump j; j.type=type; j.dist=dist; j.set("Mode",Val{false,NA,mode}); return j;
}
static void boundaries() {
    struct Expected { const char* type; double v[5], k[5]; };
    const Expected rows[]={
      {"LJ",{230,235,240,245,250},{260,265,275,285,290}},
      {"BH",{235,240,245,250,255},{315,320,330,342,350}},
      {"MBH",{235,240,245,250,255},{335,340,350,358,365}},
      {"WJ",{240,245,250,255,260},{280,285,295,305,315}},
      {"LAJ",{140,145,150,155,160},{160,165,175,185,195}},
      {"JB",{240,245,250,255,260},{280,285,295,305,315}}};
    int checks=0;
    for(const auto& row:rows) for(const char* mode:{"VNL","CKZ","KZT"}) {
        const double* th=std::string(mode)=="VNL"?row.v:row.k;
        for(int i=0;i<5;++i) for(int side=-1;side<=1;++side) {
            double d=side<0?std::nextafter(th[i],-INFINITY):side>0?std::nextafter(th[i],INFINITY):th[i];
            Jump j=make(row.type,d,mode); int rank=side<0?i:i+1;
            for (int minimum=R_IMPRESSIVE;minimum<=R_WRECKER;++minimum) {
                g_successRank=minimum;
                assert(j.rank()==rank && j.result()==(rank>=minimum?1:2));
                j.mark=1; assert(j.result()==1); j.mark=2; assert(j.result()==2);
                j.mark=0; assert(j.result()==(rank>=minimum?1:2)); ++checks;
            }
        }
        printf("PASS boundaries %s %s (below/exact/above all 5 thresholds)\n",mode,row.type);
    }
    assert(checks==1350);g_successRank=R_PERFECT;
    for(const char* mode:{"", "unknown", "VNL2", "CKZ/KZT", "VNL CKZ"}) {
        Jump j=make("LJ",300,mode); assert(j.rank()==R_UNKNOWN && j.result()==0);
        j.mark=1;assert(j.result()==1);j.mark=0;assert(j.result()==0);
    }
    assert(make("LJ",235," vNl ").rank()==R_PERFECT);
    assert(make("CJ",300,"VNL").rank()==R_UNKNOWN);
    assert(make("LJ",INFINITY,"VNL").rank()==R_UNKNOWN);
    assert(make("LJ",NAN,"VNL").rank()==R_UNKNOWN);
    const COLORREF colors[]={RGB(154,154,154),RGB(85,153,255),RGB(63,185,80),RGB(255,75,75),RGB(245,200,76),RGB(191,111,255)};
    for(int i=0;i<6;++i) assert(tone_color((Tone)(T_MEH+i))==colors[i]);
    assert(tone_color(T_FAIL)==RGB(255,155,56));
    puts("PASS 1350 rank boundaries across all five success thresholds; manual overrides; unknown modes; six rank colors + orange status");
}
static void success_policy_tests() {
    assert(parse_success_rank("")==R_PERFECT && parse_success_rank("Meh")==R_PERFECT);
    assert(parse_success_rank("invalid")==R_PERFECT && parse_success_rank("99")==R_PERFECT);
    std::deque<Jump> history;
    for(int rank=R_MEH;rank<=R_WRECKER;++rank) {
        Jump j=make("LJ",225+rank*5,"VNL");
        j.set("AvgSync",Val{true,50.0+rank*5,{}});
        j.keys.push_back({"LEFT KEY","LL$$"});Strafe st;st.n=1;st.v["Sync"]=80;j.strafes.push_back(st);
        history.push_back(j);
    }
    history.push_back(make("LJ",300,"unknown"));
    auto originalCsv=build_csv(history);const Jump* first=&history.front();
    for(int minimum=R_IMPRESSIVE;minimum<=R_WRECKER;++minimum) {
        g_successRank=minimum;
        Group good{"удачные",T_GOOD,{}},bad{"неудачные",T_FAIL,{}};
        for(const auto& j:history) {
            if(j.result()==1)good.js.push_back(&j);
            if(j.result()==2)bad.js.push_back(&j);
        }
        auto counts=lj_counts(history);
        assert(counts.total==7 && counts.good==6-minimum && counts.bad==minimum);
        assert(good.js.size()==(size_t)counts.good && bad.js.size()==(size_t)counts.bad);
        assert(std::abs(good.avg("dist")-(225+minimum*5+250)/2.0)<1e-9);
        assert(std::abs(bad.avg("dist")-(225+225+(minimum-1)*5)/2.0)<1e-9);
        assert(std::abs(good.avg("AvgSync")-(50+minimum*5+75)/2.0)<1e-9);
        assert(std::abs(bad.avg("AvgSync")-(50+50+(minimum-1)*5)/2.0)<1e-9);
        for(int rank=R_MEH;rank<=R_WRECKER;++rank) {
            assert(history[rank].rank()==rank && history[rank].dist==225+rank*5);
            assert(history[rank].keys[0].second=="LL$$" && history[rank].strafes[0].g("Sync")==80);
        }
        assert(history.back().rank()==R_UNKNOWN && history.back().result()==0);
        assert(&history.front()==first && history.size()==7);
        history.front().mark=1;history[5].mark=2;
        assert(history.front().result()==1 && history[5].result()==2);
        history.front().mark=0;history[5].mark=0;
    }
    g_successRank=R_PERFECT;assert(build_csv(history)==originalCsv);
    puts("PASS history reclassification: all five thresholds, counts, group averages, unknown mode, marks, intact raw data and CSV round trip");
}
static void parser_tests() {
    const char* types[]={"Long Jump","Bunnyhop","Multi Bunnyhop","Weird Jump","Ladder Jump","Jumpbug"};
    const char* shortTypes[]={"LJ","BH","MBH","WJ","LAJ","JB"};
    for(int i=0;i<6;++i) for(const char* mode:{"VNL","CKZ","KZT"}) {
        std::deque<Jump> parsed;Parser parser(parsed);std::string command;
        parser.feed(sfmt("player jumped 300 units with a %s",types[i]),command);
        parser.feed(std::string(mode)+" | 2 Strafes | 250 Pre",command);
        assert(parsed.size()==1 && parsed.back().type==shortTypes[i] && parsed.back().rank()!=R_UNKNOWN);
    }
    std::deque<Jump> js; Parser p(js); std::string cmd;
    assert(p.feed("player jumped 235 units with a Long Jump",cmd)==Parser::NEW);
    assert(js.back().result()==0);
    js.back().mark=2;
    assert(p.feed("VNL | 2 Strafes | 81.5% AvgSync | 250 Pre | 291 Max",cmd)==Parser::UPD);
    assert(js.back().rank()==R_PERFECT && js.back().result()==2 && js.back().mark==2);
    p.feed("#. Sync Gain Loss Airtime BadAngles Overlap DeadAir Width",cmd);
    p.feed("1. 82.4% 10.37 -0.30 50.0% 2.0% 0.0% 0.0% 16.0",cmd);
    p.feed("LEFT KEY | LLLL",cmd);
    assert(js.back().strafes.size()==1 && js.back().keys.size()==1);
    assert(js.back().mark==2); js.back().mark=0;assert(js.back().result()==1);
    p.feed("player jumped 265 units with a Long Jump",cmd);
    assert(js.back().result()==0); p.feed("Mode: CKZ | 2 Strafes | 250 Pre",cmd);
    assert(js.back().rank()==R_PERFECT && js.front().mark==0);
    js.front().mark=1;
    p.feed("player jumped 260 units with a Long Jump",cmd);
    p.feed("KZT | 1 Strafe | 75% AvgSync",cmd);
    assert(js.back().rank()==R_IMPRESSIVE && js.back().result()==2 && js.front().mark==1);
    p.feed("player jumped 300 units with a Long Jump",cmd);
    p.feed("OTHER | 2 Strafes | 250 Pre",cmd);assert(js.back().result()==0);
    assert(p.feed("LJMARK CLEAR",cmd)==Parser::CMD && cmd=="clear");
    std::string csv=build_csv(js);
    assert(csv.find("ручная_отметка")!=std::string::npos && csv.find(";Perfect;GOOD")!=std::string::npos);
    assert(csv.find(";Impressive;AUTO")!=std::string::npos);
    puts("PASS incremental parser, Mode VNL/CKZ/KZT, no inherited mode, strafes/timeline, CSV");
}

static void snapshot(const char* path) {
    RECT r{};GetClientRect(g.hwnd,&r);HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
    HBITMAP bm=CreateCompatibleBitmap(screen,r.right,r.bottom);ReleaseDC(nullptr,screen);
    HGDIOBJ old=SelectObject(dc,bm);assert(PrintWindow(g.hwnd,dc,PW_CLIENTONLY));SelectObject(dc,old);
    BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=r.right;
    bi.bmiHeader.biHeight=r.bottom;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
    std::vector<char> pixels((size_t)r.right*r.bottom*4);assert(GetDIBits(dc,bm,0,r.bottom,pixels.data(),&bi,DIB_RGB_COLORS));
    BITMAPFILEHEADER fh{};fh.bfType=0x4d42;fh.bfOffBits=sizeof fh+sizeof(BITMAPINFOHEADER);fh.bfSize=fh.bfOffBits+(DWORD)pixels.size();
    FILE* f=fopen(path,"wb");assert(f);fwrite(&fh,sizeof fh,1,f);fwrite(&bi.bmiHeader,sizeof bi.bmiHeader,1,f);fwrite(pixels.data(),pixels.size(),1,f);fclose(f);
    DeleteObject(bm);DeleteDC(dc);
}
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--reload") {
        wchar_t dir[MAX_PATH];GetCurrentDirectoryW(MAX_PATH,dir);g.iniPath=std::wstring(dir)+L"\\verification_test.ini";
        load_visual_preferences();
        assert(g.classicMenu && std::abs(g.scale-1.1)<0.001 && g.alpha==235);
        assert(!g.showStrafes && !g.showKeys && !g.showHist && !g.showCmp);
        assert(g_successRank==R_WRECKER);
        for(int rank=R_IMPRESSIVE;rank<=R_WRECKER;++rank) {
            ini_set(L"overlay",L"success_rank",W(rank_name(rank)));load_visual_preferences();
            assert(g_successRank==rank);
        }
        for(const wchar_t* value:{L"",L"Meh",L"bogus",L"999"}) {
            ini_set(L"overlay",L"success_rank",value);load_visual_preferences();assert(g_successRank==R_PERFECT);
        }
        ini_set(L"overlay",L"success_rank",L"  ImPrEsSiVe  ");load_visual_preferences();assert(g_successRank==R_IMPRESSIVE);
        ini_set(L"overlay",L"success_rank",L"wrecker");
        return 0;
    }
    setvbuf(stdout,nullptr,_IONBF,0);boundaries();parser_tests();success_policy_tests();
    g.inst=GetModuleHandleW(nullptr);g.measureDC=CreateCompatibleDC(nullptr);g.scale=1;g.dpi=96;
    wchar_t cwd[MAX_PATH];GetCurrentDirectoryW(MAX_PATH,cwd);
    g.iniPath=std::wstring(cwd)+L"\\verification_test.ini";DeleteFileW(g.iniPath.c_str());
    g.sessionFile=std::wstring(cwd)+L"\\verification_test.csv";
    g.exeDir=cwd;g.jumpsDir=cwd;
    g.classicMenu=false;apply_theme();rebuild_fonts();
    WNDCLASSW wc{};wc.lpfnWndProc=TestWnd;wc.hInstance=g.inst;wc.lpszClassName=L"LJVerificationHUD";assert(RegisterClassW(&wc));
    g.hwnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,wc.lpszClassName,L"single HUD test",WS_POPUP,20,20,800,600,nullptr,nullptr,g.inst,nullptr);assert(g.hwnd);
    SetLayeredWindowAttributes(g.hwnd,0,(BYTE)g.alpha,LWA_ALPHA);
    // Real parser fixture fills the existing HUD; no alternative renderer.
    FILE* f=fopen("sample_jump_console.log","rb");assert(f);char line[4096];std::string cmd;
    while(fgets(line,sizeof line,f))g.parser.feed(line,cmd);fclose(f);assert(g.jumps.size()==1);
    Jump base=g.jumps.back();base.dist=240;g.jumps.push_back(base);base.dist=235;base.mark=2;g.jumps.push_back(base);
    relayout();ShowWindow(g.hwnd,SW_SHOWNORMAL);UpdateWindow(g.hwnd);
    HWND same=g.hwnd;auto originalCsv=build_csv(g.jumps);
    snapshot("hud_modern.bmp");
    assert(!g.settingsOpen);
    POINT button{(g.settingsButton.left+g.settingsButton.right)/2,(g.settingsButton.top+g.settingsButton.bottom)/2};
    SendMessageW(g.hwnd,WM_LBUTTONUP,0,MAKELPARAM(button.x,button.y));assert(g.settingsOpen && g.menuHits.size()==12);
    g.menuHover=11;
    SendMessageW(g.hwnd,WM_KEYDOWN,VK_RIGHT,0);assert(g_successRank==R_GODLIKE);
    SendMessageW(g.hwnd,WM_KEYDOWN,VK_LEFT,0);assert(g_successRank==R_PERFECT);
    SendMessageW(g.hwnd,WM_KEYDOWN,VK_RETURN,0);assert(g_successRank==R_GODLIKE);
    menu_select(11);menu_select(11);assert(g_successRank==R_WRECKER);
    assert(ini_get(L"overlay",L"success_rank",L"")==L"wrecker");
    auto changedCounts=lj_counts(g.jumps);assert(changedCounts.total==3 && changedCounts.good==0 && changedCounts.bad==3);
    std::string diskCsv;assert(read_file(g.sessionFile,diskCsv) && diskCsv==build_csv(g.jumps));
    menu_select(11);assert(g_successRank==R_IMPRESSIVE);
    menu_select(11,-1);assert(g_successRank==R_WRECKER);
    for(int rank=R_WRECKER;rank>R_PERFECT;--rank)menu_select(11,-1);
    assert(build_csv(g.jumps)==originalCsv && g.hwnd==same && g.jumps.size()==3);
    assert(soundLast==0); // Policy changes never replay rank awards; no assets loaded in this test.
    puts("PASS inline success selector: arrows, Enter, cycling, INI, history counts, immediate CSV, unchanged jump data");
    menu_select(1);assert(g.classicMenu && g.hwnd==same && load_menu_style());
    assert(ini_get(L"overlay",L"style",L"")==L"cs16" && clr::accent==HEX(0xe3c86a));
    assert(build_csv(g.jumps)==originalCsv);snapshot("hud_cs16_settings.bmp");
    auto adjustClick=[&](int key,int dir){
        RECT target{};for(const auto& item:g.adjustHits)if(item.key==key && item.direction==dir)target=item.r;
        assert(target.right>target.left);
        SendMessageW(g.hwnd,WM_LBUTTONUP,0,MAKELPARAM((target.left+target.right)/2,(target.top+target.bottom)/2));
    };
    adjustClick(11,-1);assert(g_successRank==R_IMPRESSIVE);
    adjustClick(11,1);assert(g_successRank==R_PERFECT && build_csv(g.jumps)==originalCsv);
    adjustClick(2,-1);assert(std::abs(g.scale-0.9)<0.001);adjustClick(2,1);assert(std::abs(g.scale-1)<0.001);
    adjustClick(3,-1);assert(g.alpha==235);adjustClick(3,1);assert(g.alpha==250);
    double scale=g.scale;menu_select(2);assert(g.scale>scale && std::abs(pnum(U8(ini_get(L"overlay",L"scale",L"")))-g.scale)<0.001);
    int opacity=g.alpha;menu_select(3,-1);assert(g.alpha==opacity-15 && ini_int(L"overlay",L"opacity",0)==g.alpha);
    BYTE actualAlpha=0;DWORD flags=0;COLORREF key=0;assert(GetLayeredWindowAttributes(g.hwnd,&key,&actualAlpha,&flags) && actualAlpha==g.alpha);
    for(int item=4;item<=7;++item)menu_select(item);
    assert(!g.showStrafes && !g.showKeys && !g.showHist && !g.showCmp);
    assert(ini_int(L"overlay",L"show_strafes",1)==0 && ini_int(L"overlay",L"show_compare",1)==0);
    // Reload exactly the startup preferences after clearing the in-memory values.
    wchar_t self[MAX_PATH];GetModuleFileNameW(nullptr,self,MAX_PATH);
    set_success_rank(R_WRECKER);
    std::wstring childCommand=L"\""+std::wstring(self)+L"\" --reload";
    STARTUPINFOW si{};si.cb=sizeof si;PROCESS_INFORMATION child{};
    assert(CreateProcessW(nullptr,childCommand.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&child));
    assert(WaitForSingleObject(child.hProcess,10000)==WAIT_OBJECT_0);DWORD exitCode=1;GetExitCodeProcess(child.hProcess,&exitCode);
    CloseHandle(child.hThread);CloseHandle(child.hProcess);assert(exitCode==0);
    set_success_rank(R_PERFECT);
    puts("PASS new OS process reloaded style, scale, opacity and visibility through production loader");
    double savedScale=g.scale;int savedOpacity=g.alpha;
    g.classicMenu=false;g.scale=0.6;g.alpha=80;g.showStrafes=true;
    g.classicMenu=load_menu_style();g.scale=pnum(U8(ini_get(L"overlay",L"scale",L"0")));
    g.alpha=ini_int(L"overlay",L"opacity",0);g.showStrafes=ini_int(L"overlay",L"show_strafes",1)!=0;
    assert(g.classicMenu && g.scale==savedScale && g.alpha==savedOpacity && !g.showStrafes);
    apply_theme();rebuild_fonts();
    for(int item=4;item<=7;++item)menu_select(item);
    menu_select(2,-1);menu_select(3,1); // Return screenshots to initial size/alpha.
    menu_select(0);assert(!g.settingsOpen && g.hwnd==same);snapshot("hud_cs16.bmp");
    auto totals=lj_counts(g.jumps);assert(totals.total==3 && totals.good==1 && totals.bad==2);
    menu_toggle();menu_select(8);menu_select(2);int selected=g.menuSelected;
    g.jumps.push_back(base);menu_select(8);assert(g.jumps[selected].mark==1);
    command("bad");assert(g.jumps.back().mark==2 && g.jumps[selected].mark==1);
    menu_select(7);assert(g.jumps[selected].mark==0);menu_select(0);menu_select(0);
    command("clear");assert(g.jumps.back().mark==0);g.jumps.pop_back();
    menu_toggle();g.menuHover=1;
    SendMessageW(g.hwnd,WM_KEYDOWN,VK_DOWN,0);assert(g.menuHover==2);
    SendMessageW(g.hwnd,WM_KEYDOWN,VK_UP,0);assert(g.menuHover==1);
    SendMessageW(g.hwnd,WM_KEYDOWN,VK_RETURN,0);assert(!g.classicMenu && g.hwnd==same);
    SendMessageW(g.hwnd,WM_KEYDOWN,VK_ESCAPE,0);assert(!g.settingsOpen);
    SendMessageW(g.hwnd,WM_KEYDOWN,'1',0);assert(!g.classicMenu); // Closed settings ignore digits.
    // Locked HUD: only the settings button gets mouse input; the rest remains click-through.
    g.locked=true;
    POINT hit{g.settingsButton.left+2,g.settingsButton.top+2};ClientToScreen(g.hwnd,&hit);
    assert(SendMessageW(g.hwnd,WM_NCHITTEST,0,MAKELPARAM(hit.x,hit.y))==HTCLIENT);
    POINT outside{1,g.winH-2};ClientToScreen(g.hwnd,&outside);
    assert(SendMessageW(g.hwnd,WM_NCHITTEST,0,MAKELPARAM(outside.x,outside.y))==HTTRANSPARENT);
    menu_toggle();assert(g.settingsOpen);
    hit=POINT{g.settingsButton.left+2,g.settingsButton.top+2};ClientToScreen(g.hwnd,&hit);
    POINT title{g.S(20),g.settingsButton.top+2};ClientToScreen(g.hwnd,&title);
    assert(SendMessageW(g.hwnd,WM_NCHITTEST,0,MAKELPARAM(title.x,title.y))==HTCAPTION);
    assert(SendMessageW(g.hwnd,WM_NCHITTEST,0,MAKELPARAM(hit.x,hit.y))==HTCLIENT);
    assert(SendMessageW(g.hwnd,WM_NCHITTEST,0,MAKELPARAM(outside.x,outside.y))==HTCLIENT);
    SetWindowPos(g.hwnd,nullptr,45,55,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    SendMessageW(g.hwnd,WM_EXITSIZEMOVE,0,0);
    assert(ini_int(L"overlay",L"x",0)==45 && ini_int(L"overlay",L"y",0)==55);
    menu_toggle();assert(!g.settingsOpen);
    puts("PASS drag header with settings open and locked; button/settings stay interactive; position saved");
    g.locked=false;update_hud_input();
    for(auto def:{std::make_pair("good","F6"),{"bad","F7"},{"clear","F8"},{"menu","INS"},{"lock","F9"},{"hide","HOME"},{"quit","CTRL+END"}}){Hotkey h;assert(parse_hotkey(def.second,h));g.hk.push_back({def.first,h});}
    hookKeys=g.hk;hookTarget=g.hwnd;
    auto callback=[&](DWORD vk,bool down){KBDLLHOOKSTRUCT k{};k.vkCode=vk;KeyboardProc(HC_ACTION,down?WM_KEYDOWN:WM_KEYUP,(LPARAM)&k);pump(5);};
    set_success_rank(R_WRECKER);
    callback(VK_F6,true);callback(VK_F6,true);callback(VK_F6,false);assert(g.jumps.back().mark==1 && postedActions==1);
    callback(VK_F7,true);callback(VK_F7,false);assert(g.jumps.back().mark==2);
    callback(VK_F8,true);callback(VK_F8,false);assert(g.jumps.back().mark==0 && g.jumps.back().result()==2);
    set_success_rank(R_IMPRESSIVE);assert(g.jumps.back().result()==1);
    set_success_rank(R_PERFECT);
    callback(VK_INSERT,true);callback(VK_INSERT,false);assert(g.settingsOpen && g.hwnd==same);
    callback(VK_INSERT,true);callback(VK_INSERT,false);assert(!g.settingsOpen);
    assert(start_keyboard());stop_keyboard();
    puts("PASS single HUD, global themes, embedded button/settings, live scale/alpha, INI reload, visibility, locked hit testing, history, F6/F7/F8/INS");
    DestroyWindow(g.hwnd);SelectObject(g.measureDC,GetStockObject(SYSTEM_FONT));
    for(HFONT font:{g.fBig,g.fType,g.fPill,g.fSub,g.fTile,g.fTile2,g.fCap,g.fSec,g.fTbl,g.fTblB,g.fTH,g.fLbl,g.fFoot,g.fFootB,g.fHint,g.fMenuHead,g.fMenuRow,g.fMenuSmall,g.fMenuDigit})DeleteObject(font);
    DeleteDC(g.measureDC);puts("PASS verification complete; CS2 not tested");
}

