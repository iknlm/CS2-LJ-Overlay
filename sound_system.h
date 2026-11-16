// SPDX-License-Identifier: MIT
// Copyright (c) 2026 IKNLM
﻿// WinMM PCM playback. One process-local voice, no system mixer changes.
static bool soundReplay = false;
static std::map<std::string, std::vector<char>> soundAssets;
static std::vector<char> soundVoice;
static ULONGLONG soundLast = 0, soundAwardUntil = 0;
static unsigned sound_u16(const char* p) { return (unsigned char)p[0] | ((unsigned char)p[1] << 8); }
static unsigned sound_u32(const char* p) { return sound_u16(p) | (sound_u16(p+2) << 16); }
static bool sound_pcm(const std::vector<char>& v, size_t& data, size_t& bytes, unsigned& rate) {
    if(v.size()<44 || memcmp(v.data(),"RIFF",4) || memcmp(v.data()+8,"WAVE",4)) return false;
    bool format=false; data=bytes=0; rate=0;
    for(size_t p=12;p+8<=v.size();) {
        size_t n=sound_u32(v.data()+p+4), start=p+8;
        if(n>v.size()-start) return false;
        if(!memcmp(v.data()+p,"fmt ",4) && n>=16) {
            unsigned channels=sound_u16(v.data()+start+2);
            format=sound_u16(v.data()+start)==1 && (channels==1 || channels==2) && sound_u16(v.data()+start+14)==16;
            rate=sound_u32(v.data()+start+8);
        }
        if(!memcmp(v.data()+p,"data",4)) {data=start;bytes=n;}
        p=start+n+(n&1);
    }
    return format && bytes && !(bytes&1) && rate;
}
static void sound_stop() { PlaySoundW(nullptr,nullptr,0); soundVoice.clear(); soundAwardUntil=0; }
static void sound_load_settings() {
    g.soundEnabled=ini_int(L"sound",L"enabled",0)!=0;
    g.soundMaster=std::clamp(ini_int(L"sound",L"master",80),0,100);
    g.soundMenu=std::clamp(ini_int(L"sound",L"menu",60),0,100);
    g.soundAwards=std::clamp(ini_int(L"sound",L"awards",100),0,100);
}
static void sound_save_settings() {
    ini_set(L"sound",L"enabled",g.soundEnabled?L"1":L"0");
    ini_set(L"sound",L"master",std::to_wstring(g.soundMaster));
    ini_set(L"sound",L"menu",std::to_wstring(g.soundMenu));
    ini_set(L"sound",L"awards",std::to_wstring(g.soundAwards));
}
static void sound_load_assets() {
    sound_stop(); soundAssets.clear();
    for(const char* name:{"wpn_moveselect","wpn_select","wpn_denyselect","wpn_hudon","wpn_hudoff","tick","error","success","Godlike","Ownage","achievement"}) {
        std::wstring path=g.exeDir+L"\\sounds\\"+W(name)+L".wav";
        HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        if(f==INVALID_HANDLE_VALUE) continue;
        LARGE_INTEGER n{}; GetFileSizeEx(f,&n);
        if(n.QuadPart>0 && n.QuadPart<=4*1024*1024) {
            std::vector<char> v((size_t)n.QuadPart); DWORD got=0;
            if(ReadFile(f,v.data(),(DWORD)v.size(),&got,nullptr) && got==v.size()) {
                size_t d,b; unsigned r; if(sound_pcm(v,d,b,r)) soundAssets[name]=std::move(v);
            }
        }
        CloseHandle(f);
    }
}
static bool sound_play(const char* name,bool menu) {
    if(soundReplay || !g.classicMenu || !g.soundEnabled) return false;
    int volume=g.soundMaster*(menu?g.soundMenu:g.soundAwards);
    if(!volume) return false;
    ULONGLONG now=GetTickCount64();
    if((soundLast && now-soundLast<90) || (menu && now<soundAwardUntil)) return false;
    auto it=soundAssets.find(name); if(it==soundAssets.end()) return false;
    size_t data,bytes; unsigned rate;
    if(!sound_pcm(it->second,data,bytes,rate)) return false;
    // Stop before replacing the buffer: SND_MEMORY must remain alive until playback ends.
    sound_stop(); soundVoice=it->second;
    for(size_t i=data;i<data+bytes;i+=2) {
        int sample=(short)sound_u16(soundVoice.data()+i);
        short scaled=(short)(sample*volume/10000);
        soundVoice[i]=(char)(scaled&255); soundVoice[i+1]=(char)((unsigned short)scaled>>8);
    }
    bool played=PlaySoundW(reinterpret_cast<LPCWSTR>(soundVoice.data()),nullptr,SND_MEMORY|SND_ASYNC|SND_NODEFAULT)!=0;
    if(played) {soundLast=now;if(!menu) soundAwardUntil=now+bytes*1000/rate;}
    return played;
}
static void sound_rank(int rank) {
    static const char* names[]={"error","tick","success","Godlike","Ownage","achievement"};
    if(rank>=R_MEH && rank<=R_WRECKER) sound_play(names[rank],false);
}
