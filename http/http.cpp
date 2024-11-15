#include <openssl/ssl.h>
#include <openssl/err.h>
#include "http.hpp"

namespace wtk::http {
	struct Response {
		gar<char> data;

		void free() {
			this->data.free();
		}

		ar<char> get_headers() {
			for (size_t a = 0; a < this->data.len - 3; ++a) {
				if (this->data[a] == '\r' && this->data[a + 1] == '\n' && this->data[a + 2] == '\r' && this->data[a + 3] == '\n') {
					return ar<char>(this->data.buf, a);
				}
			}
			return ar<char>();
		}

		ar<char> get_header(const char* name) {
			if (this->data.buf == nullptr) {
				goto end;
			}
			{
			ar<char> headers = this->get_headers();
			size_t name_len = strlen(name);
			size_t up_to = headers.len - (name_len - 1);
			for (size_t a = 0; a < up_to; ++a) {
				if (std::memcmp(&headers[a], name, name_len) == 0) {
					a += name_len + 1;
					for (size_t b = a; b < up_to; ++b) {
						if (headers[b] == ' ') {
							a += 1;
						} else if (headers[b] == '\r' && headers[b + 1] == '\n') {
							return ar<char>(&headers[a], b - a);
						}
					}
					goto end;
				}
			}
			}
			end:
			return ar<char>();
		}

		ar<char> get_body() {
			if (this->data.buf == nullptr) {
				goto end;
			}
			for (size_t a = 0; a < this->data.len - 3; ++a) {
				if (this->data[a] == '\r' && this->data[a + 1] == '\n' && this->data[a + 2] == '\r' && this->data[a + 3] == '\n') {
					return ar<char>(&this->data[a], this->data.len - a);
				}
			}
			end:
			return ar<char>();
		}
	};

	void init() {
		SSL_load_error_strings();
		SSL_library_init();
	}

	SSL_CTX* create_ssl_context() {
		const SSL_METHOD* method;
		method = SSLv23_client_method();
		SSL_CTX* ssl_ctx;
		ssl_ctx = SSL_CTX_new(method);
		if (!ssl_ctx) {
			WTK_PANIC("SSL_CTX_new failed");
		}
		return ssl_ctx;
	}

	Response request(const char* host, const char* data) {
		Response response;
		response.data = gar<char>();

		SSL_CTX* ssl_ctx;
		ssl_ctx = create_ssl_context();

		i32 sock;
		if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			WTK_LOG("socket failed (host:%s)", host);
			goto end;
		}

		struct hostent* server;
		if ((server = gethostbyname(host)) == nullptr) {
			WTK_LOG("gethostbyname failed (host:%s)", host);
			goto end;
		}

		struct sockaddr_in server_addr;
		memset(&server_addr, 0, sizeof(server_addr));
		memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
		server_addr.sin_family = AF_INET;
		server_addr.sin_port = htons(443);

		if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
			WTK_LOG("connect failed (host:%s)", host);
			goto end_close_socket;
		}
		
		SSL* ssl;
		ssl = SSL_new(ssl_ctx);
		SSL_set_fd(ssl, sock);

		if (SSL_connect(ssl) <= 0) {
			ERR_print_errors_fp(stderr);
			goto end_clean_all;
		}

		{
			if (SSL_write(ssl, data, strlen(data)) <= 0) {
				ERR_print_errors_fp(stderr);
				goto end_clean_all;
			}
		}

		int bytes_received;
		{
			constexpr i32 buffer_size = 4096;
			response.data = gar<char>::alloc(buffer_size);
			char temp_buffer[buffer_size];
			while ((bytes_received = SSL_read(ssl, temp_buffer, buffer_size)) > 0) {
				response.data.push_many(temp_buffer, bytes_received);
			}
		}

		if (bytes_received < 0) {
			ERR_print_errors_fp(stderr);
			response.free();
			return response;
		}

		{
		ar<char> transfer_encoding = response.get_header("Transfer-Encoding");
		bool is_chunked = transfer_encoding.len == 7 && std::memcmp(transfer_encoding.buf, "chunked", 7) == 0;
		if (is_chunked) {
			// TODO: remove chunk headers
		}
		}

		response.data.push('\0');
		response.data.len -= 1;

		end_clean_all:
		SSL_free(ssl);
		SSL_CTX_free(ssl_ctx);
		EVP_cleanup();
		end_close_socket:
		close(sock);
		end:
		return response;
	}

	Response get(const char* host, const char* path) {
		char* data = alloc_format("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host).buf;
		Response response = request(host, data);
		free(data);
		return response;
	}

	Response post(const char* host, const char* path, const char* body) {
		char* data = alloc_format("POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %i\r\nConnection: close\r\n\r\n%s", path, host, strlen(body), body).buf;
		Response response = request(host, data);
		free(data);
		return response;
	}
}