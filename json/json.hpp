namespace wtk::json {
	struct Value;
	struct Field;

	struct Value {
		enum class Type {
			Error, Object, Array, String, Number, Bool, Null,
		};
		
		union {
			gar<Field> fields = gar<Field>();
			gar<Value> array;
			gar<char> string;
			double number;
			bool boolean;
		};
		Type type;

		static Value of_type(Type type);

		void free();
		Value get_value(const char*) const;
	};

	struct Field {
		gar<char> name;
		Value value;

		void free();
	};
}