#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
static std::vector<std::wstring> painted;
static BOOL WINAPI GoalTextOut(HDC dc,int x,int y,LPCWSTR s,int n) {
    painted.emplace_back(s,n); return ::TextOutW(dc,x,y,s,n);
}
#define TextOutW GoalTextOut
#undef NOMINMAX
#include "lj_overlay.cpp"
#undef TextOutW
#include <cassert>
static Jump sample(int m,int t,const std::string& value) {
    Jump j; j.type=RANK_THRESHOLDS[t].type; j.distanceText=value; j.dist=pnum(value);
    j.set("Mode",Val{false,NA,JUMP_MODES[m]}); return j;
}
static bool has(const std::string& text) {
    for(auto& s:painted) if(s.find(W(text))!=std::wstring::npos) return true;
    return false;
}
static void snapshot(const char* path) {
    ShowWindow(g.hwnd,SW_SHOWNOACTIVATE); UpdateWindow(g.hwnd); painted.clear(); RECT r{}; GetClientRect(g.hwnd,&r);
    HDC screen=GetDC(nullptr), dc=CreateCompatibleDC(screen);
    auto bmp=CreateCompatibleBitmap(screen,r.right,r.bottom); ReleaseDC(nullptr,screen);
    auto old=SelectObject(dc,bmp); assert(PrintWindow(g.hwnd,dc,PW_CLIENTONLY));
    SelectObject(dc,old);
    BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=r.right;
    bi.bmiHeader.biHeight=r.bottom; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
    std::vector<unsigned char> bits((size_t)r.right*r.bottom*4);
    assert(GetDIBits(dc,bmp,0,r.bottom,bits.data(),&bi,DIB_RGB_COLORS));
    BITMAPFILEHEADER fh{};fh.bfType=0x4d42;fh.bfOffBits=sizeof(fh)+sizeof(BITMAPINFOHEADER);fh.bfSize=fh.bfOffBits+(DWORD)bits.size();
    FILE* f=fopen(path,"wb");assert(f);fwrite(&fh,sizeof(fh),1,f);fwrite(&bi.bmiHeader,sizeof(bi.bmiHeader),1,f);fwrite(bits.data(),bits.size(),1,f);fclose(f);
    DeleteObject(bmp);DeleteDC(dc);
    printf("GDI snapshot %s %dx%d\n",path,r.right,r.bottom);
}
int main() {
    setvbuf(stdout,nullptr,_IONBF,0);
    g.inst=GetModuleHandleW(nullptr);g.measureDC=CreateCompatibleDC(nullptr);g.scale=0.8;g.dpi=96;
    wchar_t cwd[MAX_PATH];GetCurrentDirectoryW(MAX_PATH,cwd);g.exeDir=cwd;
    g.iniPath=g.exeDir+L"\\goals_test.ini";g.sessionFile=g.exeDir+L"\\goals_test.csv";
    DeleteFileW(g.iniPath.c_str());
    WNDCLASSW wc{};wc.lpfnWndProc=WndProc;wc.hInstance=g.inst;wc.lpszClassName=L"LJOverlayWnd";assert(RegisterClassW(&wc));
    g.hwnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,wc.lpszClassName,L"Goals test",WS_POPUP,20,20,800,600,nullptr,nullptr,g.inst,nullptr);assert(g.hwnd);
    SetLayeredWindowAttributes(g.hwnd,0,255,LWA_ALPHA);apply_theme();rebuild_fonts();
    int checked=0;
    for(int m=0;m<3;++m) for(int t=0;t<6;++t) {
        auto& p=g_preferences[m][t];g_prefMode=m;g_prefType=t;
        const double* ranks=m==0 ? RANK_THRESHOLDS[t].vnl:RANK_THRESHOLDS[t].kz;
        for(int r=1;r<=5;++r) {
            p.customSuccess=false;p.successRank=r;
            assert(sample(m,t,sfmt("%.1f",ranks[r-1]-0.1)).result()==2);
            assert(sample(m,t,sfmt("%.1f",ranks[r-1])).result()==1);
        }
        p.customSuccess=true;p.minimum="999.9";p.goalMode=GOAL_CUSTOM;p.target="999.9";
        for(auto value:{"999.8","999.9","1000.0"}) {
            auto j=sample(m,t,value);auto progress=goal_progress(j);
            assert(j.rank()==R_WRECKER); assert(j.result()==(std::string(value)=="999.8" ? 2:1));
            assert(progress.reached==(std::string(value)!="999.8"));
            assert(progress.text==(progress.reached ? "Цель достигнута!":"До цели: 0.1u"));
            j.mark=1;assert(j.result()==1);j.mark=2;assert(j.result()==2);j.mark=0;
            ++checked;
        }
        for(auto value:{"999.399999999999999999","999.4","999.899999999999999999","999.9"}) {
            auto j=sample(m,t,value);auto progress=goal_progress(j);
            assert(progress.isNear==(std::string(value)=="999.4" || std::string(value)=="999.899999999999999999"));
        }
        p.goalMode=GOAL_WRECKER;
        auto j=sample(m,t,sfmt("%.1f",ranks[4]-0.1));assert(goal_progress(j).text=="До Wrecker: 0.1u");
        assert(goal_progress(sample(m,t,sfmt("%.1f",ranks[4]))).reached);
        p.goalMode=GOAL_WR;p.wr="";assert(!goal_progress(j).valid);
        p.wr="1000.1";assert(goal_progress(j).text.find("WR (вручную)")!=std::string::npos);
        p.minimum=sfmt("%d.9",300+m*10+t);p.target=sfmt("%d.1",400+m*10+t);
        p.successRank=1+t%5;save_preference(true);
    }
    auto before=g_preferences[2][5];for(auto& m:g_preferences)for(auto& p:m)p=JumpPreference{};
    load_preferences();
    for(int m=0;m<3;++m)for(int t=0;t<6;++t){auto&p=g_preferences[m][t];assert(p.customSuccess && p.minimum==sfmt("%d.9",300+m*10+t));assert(p.target==sfmt("%d.1",400+m*10+t));assert(p.wr=="1000.1" && p.goalMode==GOAL_WR && p.successRank==1+t%5);}
    puts("PASS all 18 combinations: rank/custom success, below/equal/above, above Wrecker, exact decimals, Near boundary, WR manual, isolated INI roundtrip");
    g_prefMode=g_prefType=0;auto& p=g_preferences[0][0];p=JumpPreference{};p.customSuccess=true;p.minimum="249.9";p.goalMode=GOAL_CUSTOM;p.target="256.0";
    g.jumps={sample(0,0,"249.8"),sample(0,0,"249.9"),sample(0,0,"255.9")};
    for(size_t i=0;i<3;++i)g.jumps[i].attemptId=i+1;
    g.jumps[0].mark=1;g.jumps[1].mark=2;g.analyses.resize(3);g.analyses[2]=analyze_jump(g.jumps[2]);
    auto csv=build_csv(g.jumps);p.target="255.9";save_preference(false);assert(build_csv(g.jumps)==csv);assert(goal_progress(g.jumps[2]).reached);
    p.minimum="256.0";save_preference(true);assert(g.jumps[0].result()==1 && g.jumps[1].result()==2 && g.jumps[2].result()==2);
    command("good");assert(g.jumps[2].result()==1);command("bad");assert(g.jumps[2].result()==2);command("clear");assert(g.jumps[2].result()==2);
    puts("PASS history recalculation; manual overrides; F8 auto; changing target preserves CSV and counters");
    g.settingsOpen=true;g.menuPage=MENU_GOALS;g.menuHover=5;
    menu_select(7);g.goalDraft="249.9";assert(apply_goal_edit());assert(p.minimum=="249.9");
    menu_select(5,1);assert(p.minimum=="250.0");menu_select(5,-1);assert(p.minimum=="249.9");
    menu_select(8);g.goalDraft="256.0";assert(apply_goal_edit());
    menu_select(9);g.goalDraft="nan";assert(!apply_goal_edit());g.goalDraft="300.1";assert(apply_goal_edit());
    menu_select(8);g.goalDraft="999.9";SendMessageW(g.hwnd,WM_KEYDOWN,VK_ESCAPE,0);assert(p.target=="256.0");
    puts("PASS inline editing, +0.1/-0.1, invalid values rejected, Escape cancels");
    for(bool classic:{false,true}) {
        g.classicMenu=classic;apply_theme();rebuild_fonts();g.menuPage=MENU_GOALS;relayout();
        snapshot(classic?"goals_cs16.bmp":"goals_modern.bmp");
        assert(has("Своя дистанция") && has("249.9u") && has("256.0") && has("Ввести WR вручную"));
        g.settingsOpen=false;relayout();snapshot(classic?"goals_cs16_progress.bmp":"goals_modern_progress.bmp");
        assert(has("До цели: 0.1u"));
        p.target="255.9";relayout();snapshot(classic?"goals_cs16_reached.bmp":"goals_modern_reached.bmp");assert(has("Цель достигнута!"));
        p.target="256.0";g.settingsOpen=true;
    }
    g.settingsOpen=false;auto prior=p.minimum;SendMessageW(g.hwnd,WM_KEYDOWN,'5',0);assert(p.minimum==prior);
    puts("PASS real WinAPI/GDI both themes: settings, remaining, reached; closed menu ignores numbers; one HUD");
    DestroyWindow(g.hwnd);DeleteFileW(g.iniPath.c_str());return 0;
}
