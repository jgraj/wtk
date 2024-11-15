#ifndef WTK_LOG
#define WTK_LOG(...) std::printf(__VA_ARGS__);
#endif
#ifndef WTK_PANIC
#define WTK_PANIC(...) WTK_LOG(__VA_ARGS__); std::exit(1);
#endif

#include "http/http.cpp"
#include "json/json.cpp"