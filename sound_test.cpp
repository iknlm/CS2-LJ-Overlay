// SPDX-License-Identifier: MIT
// Copyright (c) 2026 IKNLM
﻿#include "lj_overlay.cpp"
#include <cassert>
int main() {
    wchar_t dir[32768]; GetCurrentDirectoryW(32768,dir); g.exeDir=dir;
    g.iniPath=g.exeDir+L"\\sound_test.ini";
    g.classicMenu=true; g.soundEnabled=true; g.soundMaster=100;g.soundMenu=100;g.soundAwards=100;
    sound_load_assets(); assert(soundAssets.size()==11);
    FILE* report=fopen("SOUND_TEST_LOG.txt","w"); assert(report);
    for(auto& [name,v]:soundAssets) {
        soundLast=0; sound_stop();
        assert(sound_play(name.c_str(),false));
        size_t d,b;unsigned rate;assert(sound_pcm(v,d,b,rate));
        fprintf(report,"PlaySoundW ASYNC accepted: %s (%zu bytes, %u bytes/sec)\n",name.c_str(),b,rate);fflush(report);
        Sleep((DWORD)(b*1000/rate+120));
    }
    sound_stop();soundLast=0;
    assert(!sound_play("missing",false));
    g.classicMenu=false;assert(!sound_play("success",false));g.classicMenu=true;
    g.soundEnabled=false;assert(!sound_play("success",false));g.soundEnabled=true;
    g.soundMaster=0;assert(!sound_play("success",false));g.soundMaster=50;
    assert(sound_play("success",false));
    auto original=soundAssets["success"];size_t d,b;unsigned rate;assert(sound_pcm(original,d,b,rate));
    for(size_t i=d;i<d+b;i+=2) assert((short)sound_u16(soundVoice.data()+i)==(short)sound_u16(original.data()+i)/2);
    assert(!sound_play("wpn_select",true));
    sound_stop();soundLast=0;g.soundMenu=0;assert(!sound_play("wpn_select",true));g.soundMenu=60;
    soundReplay=true;assert(!sound_play("success",false));soundReplay=false;
    sound_save_settings();g.soundMaster=1;g.soundMenu=1;sound_load_settings();assert(g.soundMaster==50&&g.soundMenu==60);
    g.sessionFile=g.exeDir+L"\\sound_test_session.csv";
    g.jumps.emplace_back();g.jumps.back().mark=1;soundLast=0;command("good");assert(soundLast==0);
    g.jumps.back().mark=2;command("bad");assert(soundLast==0);
    g.jumps.back().mark=0;command("clear");assert(soundLast==0);

    // Exercise live parser events and the replay watermark using a private log.
    g.measureDC=CreateCompatibleDC(nullptr);apply_theme();rebuild_fonts();
    g.settingsOpen=true;g.menuPage=MENU_MAIN;menu_select(10);assert(g.menuPage==MENU_SOUND);
    menu_select(1);assert(!g.soundEnabled);menu_select(1);assert(g.soundEnabled);
    g.soundMaster=100;menu_select(2);assert(g.soundMaster==100);
    g.soundMaster=0;menu_select(2,-1);assert(g.soundMaster==0);
    g.soundMaster=100;g.soundMenu=100;g.soundAwards=100;
    menu_select(5);assert(g.menuPage==MENU_TEST_SOUND);menu_select(0);
    g.settingsOpen=false;g.menuPage=MENU_MAIN;
    g.tail.path=g.exeDir+L"\\sound_test_console.log";
    auto logwrite=[&](const std::string& text,bool append) {
        HANDLE f=CreateFileW(g.tail.path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,append?OPEN_ALWAYS:CREATE_ALWAYS,0,nullptr);
        assert(f!=INVALID_HANDLE_VALUE);if(append)SetFilePointer(f,0,nullptr,FILE_END);
        DWORD n;assert(WriteFile(f,text.data(),(DWORD)text.size(),&n,nullptr));CloseHandle(f);
    };
    std::string jump="Tester jumped 245 units with a Long Jump\nVNL | 3 Strafes | 80% AvgSync\n";
    logwrite(jump,false);g.tail.pos=0;g.tail.replayUntil=-1;
    sound_stop();soundLast=0;on_log_timer();assert(soundLast==0); // historical
    g.tail.pos=0;g.tail.buf.clear();g.tail.replayUntil=-1;
    on_log_timer();assert(soundLast==0); // explicit reread
    logwrite(jump,true);on_log_timer();assert(soundLast!=0); // live Godlike
    sound_stop();soundLast=0;
    logwrite("VNL | 4 Strafes | 85% AvgSync\n",true);on_log_timer();assert(soundLast==0);
    on_log_timer();assert(soundLast==0); // idle cycle
    for(int rank=R_MEH;rank<=R_WRECKER;++rank) {
        sound_stop();soundLast=0;sound_rank(rank);assert(soundLast!=0);
    }
    sound_stop();soundLast=0;g.jumps.back().mark=0;command("good");assert(soundLast!=0);
    sound_stop();soundLast=0;command("bad");assert(soundLast!=0);
    sound_stop();soundLast=0;command("clear");assert(soundLast!=0);
    g.soundAwards=0;sound_stop();soundLast=0;sound_rank(R_GODLIKE);assert(soundLast==0);
    g.soundAwards=100;
    sound_stop();g.exeDir+=L"\\absent_sound_directory";sound_load_assets();assert(soundAssets.empty());assert(!sound_play("Godlike",false));
    DeleteFileW(g.tail.path.c_str());DeleteDC(g.measureDC);
    fprintf(report,"PASS: all six rank events, changed marks, historical import/reread, live award, UPD and idle deduplication, zero awards, missing WAV directory.\n");
    fprintf(report,"PASS: PCM scaling, zero master/menu, disabled, Modern, missing name, throttle, replay mute, INI, unchanged F6/F7/F8.\nPlayback API success does not prove audible output on physical speakers.\n");
    DeleteFileW(g.sessionFile.c_str());
    fclose(report);sound_stop();DeleteFileW(g.iniPath.c_str());return 0;
}
