#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
namespace lexerMetrics {
struct Counters {
    uint64_t calls=0, requestedBytes=0, fetchedBytes=0, submittedBytes=0, backtrackBytes=0;
    double lexMilliseconds=0, foldMilliseconds=0;
};
inline thread_local Counters counters;
inline thread_local bool enabled=false, active=false;
inline thread_local int64_t requestedStart=0;
inline void Reset(){counters={};enabled=true;}
inline void StartStyling(int64_t position){if(active&&position<requestedStart)counters.backtrackBytes=std::max(counters.backtrackBytes,static_cast<uint64_t>(requestedStart-position));}
inline void Fetch(int64_t position,int64_t length){
#ifdef MULTIPAD4_LEXER_METRICS
    if(active&&length>0){counters.fetchedBytes+=static_cast<uint64_t>(length);if(position<requestedStart)counters.backtrackBytes=std::max(counters.backtrackBytes,static_cast<uint64_t>(requestedStart-position));}
#else
    (void)position;(void)length;
#endif
}
inline void Submit(int64_t length){if(active&&length>0)counters.submittedBytes+=static_cast<uint64_t>(length);}
struct Scope {
    bool previous=false, recording=false, finished=false;
    std::chrono::steady_clock::time_point clock;
    Scope(int64_t start,int64_t length):previous(active),recording(enabled){if(recording){active=true;requestedStart=start;++counters.calls;counters.requestedBytes+=static_cast<uint64_t>(length);clock=std::chrono::steady_clock::now();}}
    void Fold(){if(recording){auto now=std::chrono::steady_clock::now();counters.lexMilliseconds+=std::chrono::duration<double,std::milli>(now-clock).count();clock=now;}}
    void EndFold(){if(recording){counters.foldMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-clock).count();finished=true;}}
    ~Scope(){if(recording){if(!finished)EndFold();active=previous;}}
};
}
