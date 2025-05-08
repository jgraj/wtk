struct SocketServer {
	struct TLS {
		ar<u8> cert;
		ar<u8> key;
	};

	struct Client {
		Thread thread;
		int socket_fd;
		SSL* ssl;

		void free() {
			close(this->socket_fd);
			SSL_free(ssl);
		}
	};

	Addr addr;
	SSL_CTX* ssl_ctx;
	int socket_fd;
	void* (*client_thread_func)(void*);
	Thread thread;

	static void thread_func(void* arg) {
		SocketServer* server = (SocketServer*)arg;
		struct sockaddr_in address;
		socklen_t addrlen = sizeof(address);
		bool use_tls = server->ssl_ctx != nullptr;
		while (server->thread.exists) {
			int client_socket_fd = accept(server->socket_fd, (struct sockaddr*)&address, &addrlen);
			if (client_socket_fd < 0) {
				if (errno != EWOULDBLOCK) {
					debug::warn("accept failed (%x)", errno);
				}
				usleep(10000);
				continue;
			}

			SSL* ssl = nullptr;
			if (use_tls) {
				ssl = SSL_new(server->ssl_ctx);
				SSL_set_fd(ssl, client_socket_fd);
				if (SSL_accept(ssl) <= 0) {
					debug::warn("SSL_accept failed");
					ERR_print_errors_fp(stderr);
					close(client_socket_fd);
					SSL_free(ssl);
					continue;
				}
			}

			// u32 ip_address = address.sin_addr.s_addr;
			Client* client = alloc<Client>(Client());
			client->socket_fd = client_socket_fd;
			client->ssl = ssl;
			if (pthread_create(&client->thread.id, nullptr, server->client_thread_func, (void*)client) != 0) {
				debug::warn("pthread_create failed");
				continue;
			}
		}
		close(server->socket_fd);
	}

	static SocketServer* make(bool is_async, Addr addr, TLS* tls, void* (*client_thread_func)(void*)) {
		SSL_CTX* ssl_ctx = nullptr;
		if (tls != nullptr) {
			BIO* cert_bio = BIO_new_mem_buf(tls->cert.buf, tls->cert.len);
			BIO* key_bio = BIO_new_mem_buf(tls->key.buf, tls->key.len);
			X509* cert = PEM_read_bio_X509(cert_bio, nullptr, 0, nullptr);
			EVP_PKEY* key = PEM_read_bio_PrivateKey(key_bio, nullptr, 0, nullptr);
			if (cert == nullptr || key == nullptr) {
				debug::panic("PEM_read_bio_X509/PEM_read_bio_PrivateKey failed");
			}
			ssl_ctx = SSL_CTX_new(SSLv23_server_method());
			if (ssl_ctx == nullptr) {
				debug::panic("SSL_CTX_new failed");
			}
			if (SSL_CTX_use_certificate(ssl_ctx, cert) <= 0) {
				debug::panic("SSL_CTX_use_certificate failed");
			}
			if (SSL_CTX_use_PrivateKey(ssl_ctx, key) <= 0) {
				debug::panic("SSL_CTX_use_PrivateKey failed");
			}
			if (SSL_CTX_check_private_key(ssl_ctx) != 1) {
				debug::panic("SSL_CTX_check_private_key failed");
			}
			X509_free(cert);
			BIO_free(cert_bio);
			EVP_PKEY_free(key);
			BIO_free(key_bio);
		}

		int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (socket_fd == -1) {
			debug::panic("socket failed");
		}

		int opt = 1;
		if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
			debug::panic("setsockopt failed");
		}

		int bind_result;
		if (addr.type == Addr::Type::IPv4) {
			struct sockaddr_in server_addr = {};
			server_addr.sin_family = AF_INET;
			server_addr.sin_port = htons(addr.port);
			server_addr.sin_addr.s_addr = htonl(addr.ip.v4.s_addr);
			bind_result = bind(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
		} else {
			struct sockaddr_in6 server_addr = {};
			server_addr.sin6_family = AF_INET6;
			server_addr.sin6_port = htons(addr.port);
			server_addr.sin6_addr = addr.ip.v6;
			bind_result = bind(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
		}
		if (bind_result == -1) {
			debug::panic("bind failed");
		}

		if (listen(socket_fd, 10) == -1) {
			debug::panic("listen failed");
		}

		int flags = fcntl(socket_fd, F_GETFL, 0);
		if (flags == -1 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
			debug::warn("fcntl failed");
			return nullptr;
		}

		SocketServer* server = alloc<SocketServer>(SocketServer());
		server->addr = addr;
		server->ssl_ctx = ssl_ctx;
		server->socket_fd = socket_fd;
		server->client_thread_func = client_thread_func;
		server->thread.create(thread_func, server);
		if (server->thread.exists == false) {
			debug::warn("thead.create failed");
			return nullptr;
		}
		return server;
	}
};