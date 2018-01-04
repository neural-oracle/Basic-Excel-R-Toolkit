#pragma once

/**
* class representing a function argument: name, description, default value.
* FIXME: type?
*/
class ArgumentDescriptor {
public:
    std::string name;
    std::string description;
    std::string default_value; // not necessarily a string, this is just a representation

public:
    ArgumentDescriptor(const std::string &name = "", const std::string &default_value = "", const std::string &description = "")
        :name(name)
        , description(description)
        , default_value(default_value) {}

    ArgumentDescriptor(const ArgumentDescriptor &rhs) {
        name = rhs.name;
        description = rhs.description;
        default_value = rhs.default_value;
    }
};

/** type: list of arguments */
typedef std::vector<std::shared_ptr<ArgumentDescriptor>> ARGUMENT_LIST;

/**
 * class representing a function call: name, metadata (category/description), list of arguments.
 * FIXME: return type?
 */
class FunctionDescriptor {

public:
	std::string name;
	std::string category;
	std::string description;
	ARGUMENT_LIST arguments;

	/** 
	 * this ID is assigned when we call xlfRegister, and we need to keep
	 * it around to call xlfUnregister if we are rebuilding the functions.
	 */
	int32_t registerID;

public:
	FunctionDescriptor(const std::string &name = "", const std::string &category = "", const std::string &description = "", const ARGUMENT_LIST &args = {})
		: name(name)
		, category(category)
		, description(description)
		, registerID(0)
	{
		for (auto arg : args) arguments.push_back(arg);
		std::cout << "fd ctro" << std::endl;
	}

	~FunctionDescriptor() {
		std::cout << "~fd" << std::endl;
	}

	FunctionDescriptor(const FunctionDescriptor &rhs) {
		name = rhs.name;
		category = rhs.category;
		description = rhs.description;
		registerID = rhs.registerID;
		for (auto arg : rhs.arguments) arguments.push_back(arg);
		std::cout << "fd cc" << std::endl;
	}
};

typedef std::vector<std::shared_ptr<FunctionDescriptor>> FUNCTION_LIST;

