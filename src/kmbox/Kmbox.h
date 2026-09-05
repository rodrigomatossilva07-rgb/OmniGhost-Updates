#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_  /* prevent winsock.h if winsock2 already used */
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <setupapi.h>
#include <devguid.h>
#pragma comment(lib, "setupapi.lib")

namespace kmbox
{
	extern bool connected;

	// Find COM by friendly-name substring (CH340, CP210, etc.)
	std::string find_port(const std::string& targetDescription);

	// Open using COM string (e.g. "COM3"). Empty = auto-detect.
	void KmboxInitialize(std::string port);
	// Same + custom baud (115200 default for B+/Ferrum)
	void KmboxInitializeEx(std::string port, int baud);
	void Disconnect();

	void move(int x, int y);
	void left_click();
	void left_click_release();

	bool IsDown(int virtual_key);
	bool IsKeyJustPressed(int virtual_key);
	bool IsKeyJustReleased(int virtual_key);

	std::vector<std::string> GetAvailableMouseButtons();
	int GetMouseButtonKeyCode(const std::string& button_name);
}
