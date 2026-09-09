#include "pch.h"
#include "Registry.h"
#include "Memory.h"

std::string c_registry::QueryValue(const char* path, e_registry_type type)
{
	if (!mem.vHandle)
		return "";

	BYTE buffer[0x128]{};
	DWORD _type = static_cast<DWORD>(type);
	DWORD size = sizeof(buffer);

	if (!VMMDLL_WinReg_QueryValueExU(mem.vHandle, const_cast<LPSTR>(path), &_type, buffer, &size))
	{
		LOG("[!] failed QueryValueExU call\n");
		return "";
	}

	const wchar_t* wide = reinterpret_cast<const wchar_t*>(buffer);
	size_t wideLength = static_cast<size_t>(size) / sizeof(wchar_t);
	while (wideLength > 0 && wide[wideLength - 1] == L'\0')
		--wideLength;
	if (wideLength == 0)
		return {};
	const int sourceLength = static_cast<int>(wideLength); // buffer is only 0x128 bytes
	const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, sourceLength, nullptr, 0, nullptr, nullptr);
	if (required <= 0)
		return {};
	std::string utf8(static_cast<size_t>(required), '\0');
	if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, sourceLength, utf8.data(), required, nullptr, nullptr) != required)
		return {};
	return utf8;
}
