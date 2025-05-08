namespace http {
	struct Request {
		const Addr* addr;
		ar<char> data;
		bool use_tls;

		static Request make_get(const Addr* addr, bool use_tls, const char* path);
		static Request make_post(const Addr* addr, bool use_tls, const char* path, const char* body);
		
		void destroy();
		Response send() const;
	};

	Request Request::make_get(const Addr* addr, bool use_tls, const char* path) {
		Request request;
		request.addr = addr;
		request.use_tls = use_tls;
		request.data = alloc_format("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, addr->name);
		return request;
	}

	Request Request::make_post(const Addr* addr, bool use_tls, const char* path, const char* body) {
		Request request;
		request.addr = addr;
		request.use_tls = use_tls;
		request.data = alloc_format("POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", path, addr->name, strlen(body), body);
		return request;
	}

	void Request::destroy() {
		this->data.destroy();
	}

	Response Request::send() const {
		Response response;
		response.data = gar<char>();

		SSL_CTX* ssl_ctx = SSL_CTX_new(SSLv23_client_method());
		if (ssl_ctx == nullptr) {
			WTK_PANIC("SSL_CTX_new failed");
		}

		i32 sock;
		int address_family = this->addr->type == Addr::Type::IPv4 ? AF_INET : AF_INET6;
		if ((sock = socket(address_family, SOCK_STREAM, 0)) < 0) {
			WTK_LOG("socket failed (host:%s)", this->addr->name);
			goto end;
		}

		int connect_result;
		if (this->addr->type == Addr::Type::IPv4) {
			struct sockaddr_in server_addr = {};
			server_addr.sin_family = AF_INET;
			server_addr.sin_port = htons(this->addr->port);
			server_addr.sin_addr = this->addr->ip.v4;
			connect_result = connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
		} else {
			struct sockaddr_in6 server_addr = {};
			server_addr.sin6_family = AF_INET6;
			server_addr.sin6_port = htons(this->addr->port);
			server_addr.sin6_addr = this->addr->ip.v6;
			connect_result = connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
		}
		if (connect_result < 0) {
			WTK_LOG("connect failed (host:%s)", this->addr->name);
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
		if (SSL_write(ssl, this->data.buf, this->data.len) <= 0) {
			ERR_print_errors_fp(stderr);
			goto end_clean_all;
		}
		}

		int bytes_received;
		{
		constexpr i32 buffer_size = 4096;
		response.data = gar<char>::create(buffer_size);
		char temp_buffer[buffer_size];
		while ((bytes_received = SSL_read(ssl, temp_buffer, buffer_size)) > 0) {
			response.data.push_many(temp_buffer, bytes_received);
		}
		}

		if (bytes_received < 0) {
			ERR_print_errors_fp(stderr);
			response.destroy();
			return response;
		}

		{
		ar<const char> transfer_encoding = response.get_header("Transfer-Encoding");
		bool is_chunked = transfer_encoding.len == 7 && std::memcmp(transfer_encoding.buf, "chunked", 7) == 0;
		if (is_chunked) {
			// TODO: remove chunk headers
		}
		}

		// response.data.push('\0');
		// response.data.len -= 1;

		end_clean_all:
		SSL_free(ssl);
		SSL_CTX_free(ssl_ctx);
		EVP_cleanup();
		end_close_socket:
		close(sock);
		end:
		return response;
	}
}