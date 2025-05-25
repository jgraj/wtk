namespace json {
	struct Field;

	struct Value {
		enum class Type {
			Error, Object, Array, String, Number, Bool, Null,
		};
		
		union {
			ctk::gar<Field> fields = ctk::gar<Field>();
			ctk::gar<Value> array;
			ctk::gar<u8> string;
			double number;
			bool boolean;
		};
		Type type;

		static Value of_type(Type type);
		void destroy();
		Value get_value(const char* name) const;
	};

	struct Field {
		ctk::gar<u8> name;
		Value value;

		void destroy() {
			this->name.destroy();
			this->value.destroy();
		}
	};

	Value Value::of_type(Type type) {
		Value value = Value();
		value.type = type;
		return value;
	}

	void Value::destroy() {
		switch (type) {
			case Type::Object: {
				for (size_t i = 0; i < this->fields.len; ++i) {
					this->fields[i].destroy();
				}
				this->fields.destroy();
				break;
			}
			case Type::Array: {
				for (size_t i = 0; i < this->array.len; ++i) {
					this->array[i].destroy();
				}
				this->array.destroy();
				break;
			}
			case Type::String: {
				this->string.destroy();
				break;
			}
			default: {}
		}
		this->type = Type::Error;
	}

	Value Value::get_value(const char* name) const {
		if (this->type != Type::Object) {
			return Value::of_type(Value::Type::Error);
		}
		size_t name_len = strlen(name);
		for (size_t a = 0; a < this->fields.len; ++a) {
			ctk::gar<u8> field_name = this->fields[a].name;
			if (field_name.len == name_len && std::memcmp(field_name.buf, name, name_len) == 0) {
				return this->fields[a].value;
			}
		}
		return Value::of_type(Value::Type::Error);
	}

	void skip_whitespace(ctk::ar<const u8> data, size_t* index) {
		while (*index < data.len) {
			char character = data[*index];
			if (character != ' ' && character != '\n' && character != '\r' && character != '\t') {
				return;
			}
			*index += 1;
		}
	}

	bool consume_char(ctk::ar<const u8> data, size_t* index, char expected) {
		if (*index >= data.len) {
			return false;
		}
		bool valid = data[*index] == expected;
		if (valid) {
			*index += 1;
		}
		return valid;
	}

	bool consume_str(ctk::ar<const u8> data, size_t* index, const char* expected) {
		size_t len = strlen(expected);
		if (*index + len >= data.len) {
			return false;
		}
		bool valid = std::memcmp(&data[*index], expected, len) == 0;
		if (valid) {
			*index += len;
		}
		return valid;
	}

	ctk::gar<u8> parse_string(ctk::ar<const u8> data, size_t* index) {
		ctk::gar<u8> string = ctk::gar<u8>::create_auto();
		while (*index < data.len) {
			u8 character = data[*index];
			*index += 1;
			if (character == '"') {
				return string;
			}
			if (character == '\\') {
				character = data[*index];
				switch (character) {
					case '"':
					case '\\':
					case '/': { string.push(character); break; }
					case 'b': { string.push('\b'); break; }
					case 'f': { string.push('\f'); break; }
					case 'n': { string.push('\n'); break; }
					case 'r': { string.push('\r'); break; }
					case 't': { string.push('\t'); break; }
					default: {
						goto error;
					}
				}
				*index += 1;
				continue;
			}
			if (character >= 32 && character != 127) {
				string.push(character);
				continue;
			}
			goto error;
		}
		error:
		string.destroy();
		return ctk::gar<u8>();
	}

	Value parse_value(ctk::ar<const u8>, size_t*);

	Value parse_object(ctk::ar<const u8> data, size_t* index) {
		Value value = Value::of_type(Value::Type::Object);
		value.fields = ctk::gar<Field>();
		bool was_comma = false;
		parse_field:
		skip_whitespace(data, index);
		if (!was_comma) {
			if (consume_char(data, index, '}')) {
				return value;
			} else if (value.fields.len != 0) {
				value.fields.destroy();
				return Value::of_type(Value::Type::Error);
			}
		}
		if (!consume_char(data, index, '"')) {
			value.fields.destroy();
			return Value::of_type(Value::Type::Error);
		}
		ctk::gar<u8> field_name = parse_string(data, index);
		if (field_name.buf == nullptr) {
			value.fields.destroy();
			return Value::of_type(Value::Type::Error);
		}
		skip_whitespace(data, index);
		if (!consume_char(data, index, ':')) {
			value.fields.destroy();
			return Value::of_type(Value::Type::Error);
		}
		Value field_value = parse_value(data, index);
		if (field_value.type == Value::Type::Error) {
			value.fields.destroy();
			return Value::of_type(Value::Type::Error);
		}
		if (value.fields.buf == nullptr) {
			value.fields = ctk::gar<Field>::create_auto();
		}
		value.fields.push(Field(field_name, field_value));
		skip_whitespace(data, index);
		was_comma = consume_char(data, index, ',');
		goto parse_field;
	}

	Value parse_array(ctk::ar<const u8> data, size_t* index) {
		Value value = Value::of_type(Value::Type::Array);
		value.array = ctk::gar<Value>();
		bool was_comma = false;
		parse_value:
		skip_whitespace(data, index);
		if (!was_comma) {
			if (consume_char(data, index, ']')) {
				return value;
			} else if (value.array.len != 0) {
				value.array.destroy();
				return Value::of_type(Value::Type::Error);
			}
		}
		Value elem_value = parse_value(data, index);
		if (elem_value.type == Value::Type::Error) {
			value.array.destroy();
			return Value::of_type(Value::Type::Error);
		}
		if (value.array.buf == nullptr) {
			value.array = ctk::gar<Value>::create_auto();
		}
		value.array.push(elem_value);
		skip_whitespace(data, index);
		was_comma = consume_char(data, index, ',');
		goto parse_value;
	}

	Value parse_number(ctk::ar<const u8> data, size_t* index) {
		ctk::gar<char> string = ctk::gar<char>::create_auto();
		if (data[*index] == '-') {
			string.push('-');
			*index += 1;
		}
		if (data[*index] == '0') {
			string.push('0');
			*index += 1;
		} else {
			bool first = true;
			while (true) {
				char character = data[*index];
				if (character < (first ? '1' : '0') || character > '9') {
					break;
				}
				first = false;
				string.push(character);
				*index += 1;
			}
		}
		if (data[*index] == '.') {
			string.push('.');
			*index += 1;
		}
		Value value = Value::of_type(Value::Type::Number);
		// FIXME: actually parse number
		value.number = 0.0;
		return value;
	}

	Value parse_value(ctk::ar<const u8> data, size_t* index) {
		skip_whitespace(data, index);
		if (*index >= data.len) {
			return Value::of_type(Value::Type::Error);
		}
		if (consume_char(data, index, '{')) {
			return parse_object(data, index);
		}
		if (consume_char(data, index, '[')) {
			return parse_array(data, index);
		}
		if (consume_char(data, index, '"')) {
			Value value = Value::of_type(Value::Type::String);
			value.string = parse_string(data, index);
			if (value.string.buf == nullptr) {
				return Value::of_type(Value::Type::Error);
			}
			return value;
		}
		if (consume_str(data, index, "null")) {
			return Value::of_type(Value::Type::Null);
		}
		if (consume_str(data, index, "true")) {
			Value value = Value::of_type(Value::Type::Bool);
			value.boolean = true;
			return value;
		}
		if (consume_str(data, index, "false")) {
			Value value = Value::of_type(Value::Type::Bool);
			value.boolean = false;
			return value;
		}
		return parse_number(data, index);
	}

	Value parse(ctk::ar<const u8> data) {
		size_t index = 0;
		return parse_value(data, &index);
	}
}