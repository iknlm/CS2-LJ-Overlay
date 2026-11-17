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
    std::wstring dir=g.exeDir+L"\\players_test_data";CreateDirectoryW(dir.c_str(),nullptr);
    g.iniPath=dir+L"\\test.ini";g.sessionFile=dir+L"\\test.csv";g.historyPath=dir+L"\\history.dat";
    g.tail.select(dir+L"\\console.log");g.autoLog=false;g.nextAttemptId=51;
    const std::string unicode="Игрок_猫 ★;\"LJMARK QUIT\"";
    const std::string longName="ОченьДлинныйНикИгрока_ABCDEFGHIJKLMNOPQRSTUVWXYZ_猫_1234567890" + std::string(200,'Z');
    auto block=[](const std::string& time,const std::string& name,const std::string& distance,const std::string& type,const std::string& mode,bool suffix=false) {
        return "11/17 "+time+" "+name+" jumped "+distance+" units with a "+type+(suffix?" ["+mode+"]":"")+"\n"+
            mode+" | 2 Strafes | 80% AvgSync | 250 Pre | 300 Max\n#. Sync Gain Loss\n1. 80 10 -0.1\n2. 80 10 -0.2\n";
    };
    std::string data=block("01:00:01","anonimMizantr0p","240.54033","Long Jump","VNL")+
        block("01:00:02","BlackMiddle","291.73","Multi Bunnyhop","CKZ",true)+
        block("01:00:03","Alice","240.54033","Long Jump","VNL")+
        block("01:00:04","Bob","240.54033","Long Jump","VNL")+
        block("01:00:05",unicode,"240.54033","Long Jump","KZT",true)+
        block("01:00:06",longName,"255.91","Bunnyhop","VNL")+
        "11/17 01:00:07 JS | LJ: 241.25 | 2 Strafes (80%) | VNL\n"+
        block("01:00:08","OtherBH","291.73","Bunnyhop","CKZ",true);
    assert(write_file(g.tail.path,data.data(),data.size()));
    on_log_timer();assert(g.jumps.size()==8);
    const std::vector<std::string> names={"anonimMizantr0p","BlackMiddle","Alice","Bob",unicode,longName,"","OtherBH"};
    for(size_t i=0;i<8;++i){assert(g.jumps[i].name==names[i]);assert(g.jumps[i].attemptId==51+i);}
    assert(g.jumps[0].distanceText=="240.54033" && g.jumps[0].type=="LJ");
    assert(g.jumps[1].distanceText=="291.73" && g.jumps[1].type=="MBH" && g.jumps[1].gs("Mode")=="CKZ");
    assert(g.jumps[7].type=="BH" && g.jumps[7].name=="OtherBH");
    assert(g.jumps[6].playerName()=="Неизвестный игрок");
    assert(!g.hidden);
    auto csv=build_csv(g.jumps);assert(csv.find("attempt_id")!=std::string::npos && csv.find("anonimMizantr0p")!=std::string::npos);
    assert(csv.find(csv_field(unicode))!=std::string::npos);
    for(size_t i=0;i<8;++i){auto report=analyze_jump(g.jumps[i]);assert(report[0].find("Игрок: "+g.jumps[i].playerName())!=std::string::npos);}
    puts("PASS exact sample names/distances/types; bracket CKZ/KZT; mixed players/equal distances; Unicode/special/long nicknames; anonymous compact stays unknown; CSV name+ID and report");
    select_attempt(1);command("good");assert(g.jumps[7].mark==1 && g.jumps[1].mark==0);assert(viewed_jump()->name=="BlackMiddle");
    g.settingsOpen=true;g.menuPage=MENU_JUMPS;g.menuSelected=1;menu_select(9);assert(g.jumps[1].mark==2 && g.jumps[1].name=="BlackMiddle");
    assert(g.jumps[7].name=="OtherBH");
    csv=build_csv(g.jumps);save_csv();auto next=g.nextAttemptId;
    g.parser.reset();g.importer=ConsoleImport{};g.jumps.clear();g.analyses.clear();g.nextAttemptId=1;follow_latest();
    assert(history_store::load());assert(g.nextAttemptId==next && g.jumps.size()==8 && build_csv(g.jumps)==csv);
    assert(g.jumps[1].mark==2 && g.jumps[1].name=="BlackMiddle");
    g.tail.select(g.tail.path);on_log_timer();assert(g.jumps.size()==8 && build_csv(g.jumps)==csv);
    g.tail=Tailer{};g.tail.path=dir+L"\\console.log";on_log_timer();assert(g.jumps.size()==8 && build_csv(g.jumps)==csv);
    auto repeated=data+block("01:00:03","Alice","240.54033","Long Jump","VNL");
    assert(write_file(g.tail.path,repeated.data(),repeated.size()));on_log_timer();assert(g.jumps.size()==8);
    puts("PASS snapshot cold restore name+original ID+marks+metrics, replay/import/reinitialized Tailer and repeated stamped header without duplicates");
    // A missing/rotated console must not erase the restored names/IDs.
    assert(DeleteFileW(g.tail.path.c_str()));g.jumps.clear();g.parser.reset();assert(history_store::load());on_log_timer();assert(g.jumps.size()==8 && g.jumps[1].attemptId==52);
    std::string valid;assert(read_file(g.historyPath,valid));auto damaged=valid;damaged[damaged.size()/2]^=1;
    assert(write_file(g.historyPath,damaged.data(),damaged.size()));assert(!history_store::load());assert(g.jumps.size()==8 && g.jumps[1].name=="BlackMiddle");
    assert(write_file(g.historyPath,valid.data(),valid.size()));
    puts("PASS restore with missing console; corrupted snapshot rejected without partial mutation");
    // A rejected/foreign header is a boundary, not a nickname update for the previous jump.
    std::deque<Jump> isolated;Parser parser(isolated);ConsoleImport importer;parser.player="anonimMizantr0p";std::string cmd;
    importer.feed(parser,"anonimMizantr0p jumped 240.54033 units with a Long Jump [VNL]","filter",0,cmd);
    importer.feed(parser,"BlackMiddle jumped 291.73 units with a Multi Bunnyhop [CKZ]","filter",100,cmd);
    importer.feed(parser,"CKZ | 2 Strafes | 99% AvgSync","filter",200,cmd);
    assert(isolated.size()==1 && isolated[0].name=="anonimMizantr0p" && isolated[0].gs("Mode")=="VNL" && !ok(isolated[0].g("AvgSync")));
    importer.feed(parser,"JS | MBH: 291.73 | CKZ","filter",300,cmd);assert(isolated.size()==1);
    puts("PASS filtered foreign header/details and unnamed summary cannot rename/update last user jump");
    {
        std::deque<Jump> modes;Parser parser(modes);ConsoleImport adapter;std::string cmd;
        const std::string header="11/17 01:02:03 SamePlayer jumped 240.54033 units with a Long Jump";
        for(int pass=0;pass<2;++pass) {
            adapter.reset_stream(parser);
            long long offset=pass*1000;
            for(const auto* mode:{"VNL","CKZ","KZT"}) {
                adapter.feed(parser,header,"modes",offset++,cmd);
                adapter.feed(parser,std::string(mode)+" | 2 Strafes | 250 Pre","modes",offset++,cmd);
            }
            assert(modes.size()==3);
            for(size_t m=0;m<3;++m)assert(modes[m].name=="SamePlayer" && modes[m].gs("Mode")==JUMP_MODES[m]);
        }
        puts("PASS equal source/header/time/player/distance/type in three different modes remains three attempts; shifted replay resolves by mode without duplicates");
    }
    WNDCLASSW wc{};wc.lpfnWndProc=WndProc;wc.hInstance=g.inst;wc.lpszClassName=L"LJOverlayWnd";assert(RegisterClassW(&wc));
    g.hwnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,wc.lpszClassName,L"Players test",WS_POPUP,20,20,800,600,nullptr,nullptr,g.inst,nullptr);assert(g.hwnd);
    SetLayeredWindowAttributes(g.hwnd,0,255,LWA_ALPHA);g.settingsOpen=false;g.statusLog.clear();g.histRows=8;
    for(bool classic:{false,true}) {
        g.classicMenu=classic;apply_theme();rebuild_fonts();select_attempt(1);relayout();
        snapshot(classic?"players_cs16.bmp":"players_modern.bmp");
        assert(has("ИГРОК") && has("Попытка #52 · BlackMiddle") && has("MBH"));
        g.analysisOpen=true;g.analyses[1]=analyze_jump(g.jumps[1]);relayout();
        snapshot(classic?"players_cs16_analysis.bmp":"players_modern_analysis.bmp");assert(has("Игрок: BlackMiddle") && has("Попытка #52"));
        select_attempt(5);g.analysisOpen=false;relayout();snapshot(classic?"players_cs16_long.bmp":"players_modern_long.bmp");assert(has("…"));
        g.analysisOpen=true;relayout();snapshot(classic?"players_cs16_long_analysis.bmp":"players_modern_long_analysis.bmp");
        g.analysisOpen=false;
        select_attempt(6);relayout();snapshot(classic?"players_cs16_unknown.bmp":"players_modern_unknown.bmp");assert(has("Неизвестный"));
    }
    puts("PASS actual GDI both themes: selected matching name/type/ID, history player column, analysis player, Unicode-safe ellipsis and unknown player");
    new_session();auto afterClear=g.nextAttemptId;g.nextAttemptId=1;assert(history_store::load());assert(g.jumps.empty() && g.nextAttemptId==afterClear);
    puts("PASS explicit new session persists empty history without reusing IDs");
    DestroyWindow(g.hwnd);return 0;
}
