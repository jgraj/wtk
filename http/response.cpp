struct HTTP_Response {
	struct Status {
		ctk::ar<u8> data;
		
		void create() {
			this->data = ctk::ar<u8>();
		}

		void destroy() {
			this->data.destroy();
		}
	};
	
	struct Headers {
		ctk::ar<u8> data;

		void create() {
			this->data = ctk::ar<u8>();
		}

		void destroy() {
			this->data.destroy();
		}

		ctk::ar<const u8> get_header(const char* name) const {
			size_t name_len = std::strlen(name);
			size_t min_header_len = name_len + std::strlen(": *\n");
			size_t up_to;
			if (this->data.len < min_header_len) {
				goto end;
			}
			{
				up_to = this->data.len - min_header_len;
			}
			for (size_t a = 0; a < up_to; ++a) {
				if (ctk::astr_nocase_cmp(&this->data[a], name, name_len)) {
					a += name_len;
					if (this->data[a] == ':' && this->data[a + 1] == ' ') {
						a += 2;
						for (size_t b = a; b < up_to; ++b) {
							if (this->data[b] == '\n') {
								return ctk::ar<const u8>(&this->data[a], b - a);
							}
						}
					}
					goto end;
				}
			}
			end:
			return ctk::ar<const u8>();
		}
	};

	struct Body {
		ctk::ar<u8> data;

		void create() {
			this->data = ctk::ar<u8>();
		}

		void destroy() {
			this->data.destroy();
		}
	};

	Status status;
	Headers headers;
	Body body;

	void create() {
		this->status.create();
		this->headers.create();
		this->body.create();
	}

	void destroy() {
		this->status.destroy();
		this->headers.destroy();
		this->body.destroy();
	}
};