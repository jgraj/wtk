struct HTTP_Request {
	const Addr* addr;
	ctk::ar<u8> data;
	bool use_tls;

	static HTTP_Request make_get(const Addr* addr, bool use_tls, const char* path) {
		HTTP_Request HTTP_Request;
		HTTP_Request.addr = addr;
		HTTP_Request.use_tls = use_tls;
		HTTP_Request.data = ctk::alloc_format("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, addr->name);
		return HTTP_Request;
	}

	static HTTP_Request make_post(const Addr* addr, bool use_tls, const char* path, ctk::ar<const u8> headers, ctk::ar<const u8> body) {
		HTTP_Request HTTP_Request;
		HTTP_Request.addr = addr;
		HTTP_Request.use_tls = use_tls;
		HTTP_Request.data = ctk::alloc_format("POST %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %zu\r\nConnection: close%.*s\r\n\r\n%.*s", path, addr->name, body.len, headers.len, headers.buf, body.len, body.buf);
		return HTTP_Request;
	}

	void destroy() {
		this->data.destroy();
	}

	HTTP_Response send() const {
		HTTP_Response response;
		response.create();
		int socket_fd;
		int address_family = this->addr->type == Addr::Type::IPv4 ? AF_INET : AF_INET6;
		if ((socket_fd = ::socket(address_family, SOCK_STREAM, 0)) < 0) {
			WTK_LOG("socket failed (host:%s)", this->addr->name);
			goto end;
		}

		{
			int connect_result;
			if (this->addr->type == Addr::Type::IPv4) {
				struct sockaddr_in server_addr = {};
				server_addr.sin_family = AF_INET;
				server_addr.sin_port = ::htons(this->addr->port);
				server_addr.sin_addr = this->addr->ip.v4;
				connect_result = ::connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
			} else {
				struct sockaddr_in6 server_addr = {};
				server_addr.sin6_family = AF_INET6;
				server_addr.sin6_port = ::htons(this->addr->port);
				server_addr.sin6_addr = this->addr->ip.v6;
				connect_result = ::connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
			}
			if (connect_result < 0) {
				WTK_LOG("connect failed (host:%s)", this->addr->name);
				goto end_close_socket;
			}
		}
		
		SSL_CTX* ssl_ctx;
		SSL* ssl;
		if (this->use_tls) {
			ssl_ctx = ::SSL_CTX_new(::TLS_client_method());
			if (ssl_ctx == nullptr) {
				WTK_PANIC("SSL_CTX_new failed");
			}
			ssl = ::SSL_new(ssl_ctx);
			::SSL_set_fd(ssl, socket_fd);
			if (::SSL_connect(ssl) <= 0) {
				::ERR_print_errors_fp(stderr);
				goto end_clean_all;
			}
		} else {
			ssl_ctx = nullptr;
			ssl = nullptr;
		}

		if (this->use_tls) {
			if (::SSL_write(ssl, this->data.buf, this->data.len) <= 0) {
				::ERR_print_errors_fp(stderr);
				goto end_clean_all;
			}
		} else {
			::send(socket_fd, this->data.buf, this->data.len, MSG_NOSIGNAL);
		}

		{
			constexpr i32 buffer_size = 4096;
			enum class State {
				Status,
				Header,
				Body,
				ChunkedBodySize,
				ChunkedBodyData,
			};
			State state = State::Status;
			bool got_carriege_return = false;
			ctk::gar<u8> status = ctk::gar<u8>::create_auto();
			ctk::gar<u8> headers = ctk::gar<u8>();
			ctk::gar<u8> body = ctk::gar<u8>();
			u8 temp_buffer[buffer_size];
			int bytes_received;
			while (true) {
				if (this->use_tls) {
					bytes_received = ::SSL_read(ssl, temp_buffer, buffer_size);
				} else {
					bytes_received = ::recv(socket_fd, temp_buffer, buffer_size, 0);
				}
				if (bytes_received == 0) {
					break;
				}
				if (bytes_received < 0) {
					status.destroy();
					headers.destroy();
					body.destroy();
					::ERR_print_errors_fp(stderr);
					goto end_clean_all;
				}
				if (state == State::Body) {
					body.push_many(temp_buffer, bytes_received);
					continue;
				}
				int temp_buffer_offset = 0;
				int crlf_index = bytes_received;
				for (int a = 0; a < bytes_received; ++a) {
					if (got_carriege_return) {
						if (temp_buffer[a] != '\n') {
							goto end_clean_all;
						}
						got_carriege_return = false;
						temp_buffer_offset = a + 1;
						continue;
					}
					if (temp_buffer[a] == '\r') {
						got_carriege_return = true;
						crlf_index = a;
					}
					if (got_carriege_return || a == bytes_received - 1) {
						switch (state) {
							case State::Status: {
								status.push_many(&temp_buffer[temp_buffer_offset], crlf_index - temp_buffer_offset);
								if (got_carriege_return) {
									response.status = HTTP_Response::Status(status.to_ar());
									state = State::Header;
									headers = ctk::gar<u8>::create_auto();
								}
								break;
							}
							case State::Header: {
								if (got_carriege_return && crlf_index == temp_buffer_offset) {
									response.headers = HTTP_Response::Headers(headers.to_ar());
									state = State::Body;
									body = ctk::gar<u8>::create_auto();
									ctk::ar<const u8> transfer_encoding = response.headers.get_header("transfer-encoding");
									if (transfer_encoding.buf != nullptr) {
										const char* chunked_encoding = "chunked";
										if (ctk::astr_nocase_cmp(transfer_encoding.buf, chunked_encoding, std::strlen(chunked_encoding))) {
											state = State::ChunkedBodySize;
										}
									}
									continue;
								}
								headers.push_many(&temp_buffer[temp_buffer_offset], crlf_index - temp_buffer_offset);
								if (got_carriege_return) {
									headers.push('\n');
								}
								break;
							}
							case State::ChunkedBodySize:
							case State::ChunkedBodyData: {
								if (got_carriege_return && crlf_index == temp_buffer_offset) {
									goto response_finished;
								}
								if (state == State::ChunkedBodyData) {
									body.push_many(&temp_buffer[temp_buffer_offset], crlf_index - temp_buffer_offset);
								}
								if (got_carriege_return) {
									state = state == State::ChunkedBodySize ? State::ChunkedBodyData : State::ChunkedBodySize;
								}
								break;
							}
							default: {
								WTK_PANIC("invalid State");
								break;
							}
						}
						temp_buffer_offset = crlf_index;
						crlf_index = bytes_received;
					}
				}
			}
			if (state == State::Status || state == State::Header) {
				status.destroy();
				headers.destroy();
				response.create();
				goto end_clean_all;
			}
			response_finished:
			response.body = HTTP_Response::Body(body.to_ar());
		}

		end_clean_all:
		::SSL_free(ssl);
		::SSL_CTX_free(ssl_ctx);
		EVP_cleanup();
		end_close_socket:
		::close(socket_fd);
		end:
		return response;
	}
};