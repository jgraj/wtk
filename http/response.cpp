namespace http {
	struct Response {
		gar<char> data;

		void destroy();
		bool is_valid() const;
		ar<const char> get_headers() const;
		ar<const char> get_header(const char* name) const;
		size_t get_body_index() const;
		ar<const char> get_body() const;
	};

	void Response::destroy() {
		this->data.destroy();
	}

	bool Response::is_valid() const {
		return this->data.buf != nullptr;
	}

	ar<const char> Response::get_headers() const {
		for (size_t a = 0; a < this->data.len - 3; ++a) {
			if (this->data[a] == '\r' && this->data[a + 1] == '\n' && this->data[a + 2] == '\r' && this->data[a + 3] == '\n') {
				return ar<const char>(this->data.buf, a);
			}
		}
		return ar<const char>();
	}

	ar<const char> Response::get_header(const char* name) const {
		ar<const char> headers = this->get_headers();
		size_t name_len = strlen(name);
		size_t up_to = headers.len - (name_len - 1);
		for (size_t a = 0; a < up_to; ++a) {
			if (std::memcmp(&headers[a], name, name_len) == 0) {
				a += name_len + 1;
				for (size_t b = a; b < up_to; ++b) {
					if (headers[b] == ' ') {
						a += 1;
					} else if (headers[b] == '\r' && headers[b + 1] == '\n') {
						return ar<const char>(&headers[a], b - a);
					}
				}
				goto end;
			}
		}
		end:
		return ar<const char>();
	}

	size_t Response::get_body_index() const {
		for (size_t a = 0; a < this->data.len - 3; ++a) {
			if (this->data[a] == '\r' && this->data[a + 1] == '\n' && this->data[a + 2] == '\r' && this->data[a + 3] == '\n') {
				return a + 4;
			}
		}
		return 0;
	}

	ar<const char> Response::get_body() const {
		size_t body_index = this->get_body_index();
		return ar<const char>(&this->data[body_index], this->data.len - body_index);
	}
}