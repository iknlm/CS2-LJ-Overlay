// Versioned local session snapshot. The importer keys are saved with the exact
// attempts, so replay never matches by distance alone or by the last player.
namespace history_store {
struct Writer {
    std::string data;
    template<class T> void number(T n) { data.append(reinterpret_cast<const char*>(&n),sizeof n); }
    void text(const std::string& s) { number<unsigned long long>(s.size()); data+=s; }
};
struct Reader {
    const std::string& data; size_t pos=0; bool valid=true;
    template<class T> T number() {
        T n{}; if(!valid || data.size()-pos<sizeof n) { valid=false; return n; }
        memcpy(&n,data.data()+pos,sizeof n);pos+=sizeof n;return n;
    }
    size_t count(size_t limit=1000000) {
        auto n=number<unsigned long long>();
        if(n>limit || n>data.size()-pos) { valid=false;return 0; } return (size_t)n;
    }
    std::string text() {
        auto n=count(1024*1024);if(!valid)return "";
        std::string s=data.substr(pos,n);pos+=n;return s;
    }
};
static unsigned long long checksum(const std::string& s) {
    unsigned long long n=1469598103934665603ull;
    for(unsigned char c:s) { n^=c;n*=1099511628211ull; } return n;
}
static bool save() {
    if(g.historyPath.empty()) return true; // Isolated test harnesses opt in explicitly.
    Writer w;w.text("LJOverlay history 1");w.text(lower(g.parser.player));w.number(g.nextAttemptId);
    w.number<unsigned long long>(g.jumps.size());
    for(const auto& j:g.jumps) {
        w.number(j.attemptId);w.number<int>(j.direction);w.number(j.mark);w.number(j.dist);
        for(const auto* s:{&j.name,&j.type,&j.typeFull,&j.time,&j.stamp,&j.distanceText})w.text(*s);
        w.number<unsigned long long>(j.stats.size());
        for(const auto& [key,val]:j.stats) { w.text(key);w.number<unsigned char>(val.num);w.number(val.d);w.text(val.s); }
        w.number<unsigned long long>(j.order.size());for(const auto& key:j.order)w.text(key);
        w.number<unsigned long long>(j.strafes.size());
        for(const auto& row:j.strafes) {
            w.number(row.n);w.number<unsigned long long>(row.v.size());
            for(const auto& [key,val]:row.v) {w.text(key);w.number(val);}
        }
        w.number<unsigned long long>(j.keys.size());for(const auto& [key,val]:j.keys){w.text(key);w.text(val);}
    }
    for(const auto* map:{&g.importer.records,&g.importer.stamped}) {
        w.number<unsigned long long>(map->size());
        for(const auto& [key,index]:*map) {w.text(key);w.number<unsigned long long>(index);}
    }
    auto hash=checksum(w.data);w.number(hash);
    std::wstring tmp=g.historyPath+L".tmp";
    return write_file(tmp,w.data.data(),w.data.size()) &&
        MoveFileExW(tmp.c_str(),g.historyPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
static bool load() {
    if(g.historyPath.empty())return true;
    HANDLE file=CreateFileW(g.historyPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND;
    LARGE_INTEGER size{};bool good=GetFileSizeEx(file,&size) && size.QuadPart>=8 && size.QuadPart<=256*1024*1024;
    std::string data;DWORD got=0;
    if(good){data.resize((size_t)size.QuadPart);good=ReadFile(file,data.data(),(DWORD)data.size(),&got,nullptr) && got==data.size();}
    CloseHandle(file);if(!good)return false;
    unsigned long long hash;memcpy(&hash,data.data()+data.size()-8,8);data.resize(data.size()-8);
    if(checksum(data)!=hash)return false;
    Reader r{data};if(r.text()!="LJOverlay history 1")return false;
    auto filter=r.text();
    auto next=r.number<unsigned long long>();
    if(!r.valid || !next || next==~0ull)return false;
    if(filter!=lower(g.parser.player)) {
        g.nextAttemptId=std::max(g.nextAttemptId,next); return true;
    } // A changed filter starts a separate view without reusing saved IDs.
    std::deque<Jump> jumps;std::map<unsigned long long,bool> ids;
    size_t total=r.count();
    for(size_t n=0;n<total && r.valid;++n) {
        Jump j;j.attemptId=r.number<unsigned long long>();int direction=r.number<int>();j.direction=(JumpDirection)direction;
        j.mark=r.number<int>();j.dist=r.number<double>();
        if(!j.attemptId || j.attemptId==~0ull || ids[j.attemptId] || direction<0 || direction>3 || j.mark<0 || j.mark>2) return false;
        ids[j.attemptId]=true;next=std::max(next,j.attemptId+1);
        for(auto* s:{&j.name,&j.type,&j.typeFull,&j.time,&j.stamp,&j.distanceText})*s=r.text();
        size_t stats=r.count(10000);
        for(size_t i=0;i<stats && r.valid;++i){auto key=r.text();Val v;v.num=r.number<unsigned char>()!=0;v.d=r.number<double>();v.s=r.text();j.stats[key]=v;}
        size_t order=r.count(10000);for(size_t i=0;i<order && r.valid;++i)j.order.push_back(r.text());
        size_t rows=r.count(10000);
        for(size_t i=0;i<rows && r.valid;++i) {
            Strafe row;row.n=r.number<int>();size_t fields=r.count(10000);
            for(size_t k=0;k<fields && r.valid;++k){auto key=r.text();row.v[key]=r.number<double>();}
            j.strafes.push_back(std::move(row));
        }
        size_t keys=r.count(10000);for(size_t i=0;i<keys && r.valid;++i){auto key=r.text();auto value=r.text();j.keys.push_back({key,value});}
        jumps.push_back(std::move(j));
    }
    ConsoleImport importer;
    for(auto* map:{&importer.records,&importer.stamped}) {
        size_t count=r.count();
        for(size_t i=0;i<count && r.valid;++i){auto key=r.text();auto index=r.number<unsigned long long>();if(index>=jumps.size())return false;(*map)[key]=(size_t)index;}
    }
    if(!r.valid || r.pos!=data.size() || !next || next==~0ull)return false;
    g.parser.reset();g.jumps=std::move(jumps);g.importer=std::move(importer);g.nextAttemptId=next;
    g.analyses.resize(g.jumps.size());
    for(size_t i=0;i<g.jumps.size();++i)g.analyses[i]=analyze_jump(g.jumps[i]);
    return true;
}
}
