#ifndef WTK_LOG
#define WTK_LOG(...) std::printf(__VA_ARGS__);
#endif
#ifndef WTK_PANIC
#define WTK_PANIC(...) WTK_LOG(__VA_ARGS__); std::exit(1);
#endif

#include <netdb.h>
#include <fcntl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

namespace wtk {
	#include "addr.cpp"
	#include "socket/server.cpp"
	#include "socket/client.cpp"
	#include "http/response.cpp"
	#include "http/request.cpp"
	#include "json.cpp"

	void init() {
		::SSL_library_init();
		::OpenSSL_add_all_algorithms();
		::SSL_load_error_strings();
	}
}