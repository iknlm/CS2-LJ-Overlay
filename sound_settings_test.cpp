#include "lj_overlay.cpp"
#include <cassert>
int main(int argc,char** argv) {
 wchar_t cwd[32768];GetCurrentDirectoryW(32768,cwd);g.exeDir=cwd;
 g.iniPath=g.exeDir+L"\\settings_roundtrip.ini";
 if(argc>1) {
  sound_load_settings();
  assert(g.soundEnabled && g.soundMaster==30 && g.soundMenu==40 && g.soundAwards==50);
  puts("PASS fresh process reload: enabled=1 master=30 menu=40 awards=50");return 0;
 }
 assert(!g.soundEnabled);
 DeleteFileW(g.iniPath.c_str());sound_load_settings();
 assert(!g.soundEnabled && g.soundMaster==80 && g.soundMenu==60 && g.soundAwards==100);
 std::wstring ini=L"\xFEFF";ini+=DEFAULT_INI;
 assert(write_file(g.iniPath,ini.data(),ini.size()*sizeof(wchar_t)));
 assert(ini_int(L"sound",L"enabled",-1)==0);sound_load_settings();assert(!g.soundEnabled);
 puts("PASS no INI / generated DEFAULT_INI: sound disabled, 80/60/100");
 ini_set(L"sound",L"enabled",L"1");sound_load_settings();assert(g.soundEnabled);
 g.soundMaster=30;g.soundMenu=40;g.soundAwards=50;sound_save_settings();
 g.soundEnabled=false;g.soundMaster=g.soundMenu=g.soundAwards=0;sound_load_settings();
 assert(g.soundEnabled && g.soundMaster==30 && g.soundMenu==40 && g.soundAwards==50);
 g.measureDC=CreateCompatibleDC(nullptr);apply_theme();rebuild_fonts();
 set_menu_style(false);sound_load_settings();assert(g.soundEnabled);assert(!sound_play("success",false));
 set_menu_style(true);sound_load_settings();assert(g.soundEnabled);
 g.settingsOpen=true;g.menuPage=MENU_SOUND;
 menu_select(1);assert(!g.soundEnabled);sound_load_settings();assert(!g.soundEnabled);
 menu_select(1);menu_select(2);menu_select(3,-1);menu_select(4);
 sound_load_settings();assert(g.soundEnabled&&g.soundMaster==40&&g.soundMenu==30&&g.soundAwards==60);
 puts("PASS menu toggle/volume persistence and theme changes");
 ini_set(L"sound",L"master",L"-10");ini_set(L"sound",L"menu",L"110");ini_set(L"sound",L"awards",L"0");
 sound_load_settings();assert(g.soundMaster==0&&g.soundMenu==100&&g.soundAwards==0);
 g.soundMaster=30;g.soundMenu=40;g.soundAwards=50;sound_save_settings();
 puts("PASS bounds 0..100, explicit enabled=1 preserved");return 0;
}
