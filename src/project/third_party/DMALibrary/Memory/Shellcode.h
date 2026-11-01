#pragma once
#include "../pch.h"


class c_shellcode
{
private:

public:
	c_shellcode() = default;

	~c_shellcode() = default;







	uint64_t find_codecave(size_t function_size, const std::string& process_name, const std::string& module);







	std::vector<uint64_t> find_all_codecave(size_t function_size, const std::string& process_name);







	bool call_function(void* hook, void* function, const std::string& process_name);



































































};
