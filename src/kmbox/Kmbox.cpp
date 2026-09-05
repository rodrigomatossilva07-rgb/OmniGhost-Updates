#include "kmbox.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include "../platform/text_encoding.h"
#include "../platform/unique_handle.h"

namespace kmbox
{
	OmniGhost::Platform::UniqueHandle serial_handle;
	bool connected = false;

	static int clamp(int i)
	{
		if (i > 127) i = 127;
		if (i < -128) i = -128;
		return i;
	}

	static std::string NormalizeCom(std::string port)
	{
		// Accept "COM3", "com3", "\\\\.\\COM3"
		while (!port.empty() && (port.back() == ' ' || port.back() == '\t'))
			port.pop_back();
		while (!port.empty() && (port.front() == ' ' || port.front() == '\t'))
			port.erase(port.begin());
		if (port.size() >= 4 && port[0] == '\\')
			return port; // already \\.\COM?
		// uppercase COM
		for (auto& c : port) c = (char)toupper((unsigned char)c);
		if (port.rfind("COM", 0) == 0)
			return std::string("\\\\.\\") + port;
		return std::string("\\\\.\\") + port;
	}

	std::string find_port(const std::string& targetDescription)
	{
		HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
		if (hDevInfo == INVALID_HANDLE_VALUE) return "";

		SP_DEVINFO_DATA deviceInfoData{};
		deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

		for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &deviceInfoData); ++i)
		{
			char buf[512]{};
			DWORD nSize = 0;
			if (!SetupDiGetDeviceRegistryPropertyA(hDevInfo, &deviceInfoData, SPDRP_FRIENDLYNAME,
				NULL, (PBYTE)buf, sizeof(buf), &nSize) || nSize == 0)
				continue;
			buf[sizeof(buf) - 1] = 0;
			std::string deviceDescription = buf;
			std::string upper = deviceDescription;
			for (auto& c : upper) c = (char)toupper((unsigned char)c);
			std::string target = targetDescription;
			for (auto& c : target) c = (char)toupper((unsigned char)c);

			if (upper.find(target) == std::string::npos)
				continue;

			size_t comPos = upper.find("COM");
			if (comPos == std::string::npos) continue;
			size_t end = comPos + 3;
			while (end < upper.size() && isdigit((unsigned char)upper[end])) ++end;
			SetupDiDestroyDeviceInfoList(hDevInfo);
			return deviceDescription.substr(comPos, end - comPos);
		}
		SetupDiDestroyDeviceInfoList(hDevInfo);
		return "";
	}

	static std::string AutoDetectPort()
	{
		// Common USB-UART chips used by Kmbox B+ / Ferrum / Pro serial
		const char* probes[] = {
			"CH340",
			"CH341",
			"CP210",
			"FT232",
			"FTDI",
			"USB-SERIAL",
			"USB SERIAL",
			"SILICON LABS",
			"ARDUINO",
			"KMBOX",
			"FERRUM",
		};
		for (const char* p : probes) {
			std::string com = find_port(p);
			if (!com.empty())
				return com;
		}
		return "";
	}

	bool OpenSerial(const std::string& comPort, int baudRate)
	{
		connected = false;
		serial_handle.reset();

		const std::string path = NormalizeCom(comPort);
		std::clog << "[DEBUG][Kmbox] Opening " << path << " @ " << baudRate << std::endl;
		const std::wstring widePath = OmniGhost::Platform::Utf8ToWide(path);
		if (widePath.empty()) {
			std::cerr << "[Kmbox] Invalid UTF-8 serial path" << std::endl;
			return false;
		}

		const HANDLE openedSerial = CreateFileW(
			widePath.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			0, nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, nullptr);

		if (openedSerial == INVALID_HANDLE_VALUE) {
			std::cerr << "[Kmbox] Failed to open serial (err=" << GetLastError() << ")" << std::endl;
			return false;
		}
		serial_handle.reset(openedSerial);

		SetupComm(serial_handle.get(), 8192, 8192);

		COMMTIMEOUTS timeouts{};
		timeouts.ReadIntervalTimeout = MAXDWORD;
		timeouts.ReadTotalTimeoutMultiplier = 0;
		timeouts.ReadTotalTimeoutConstant = 0;
		timeouts.WriteTotalTimeoutMultiplier = 0;
		timeouts.WriteTotalTimeoutConstant = 2000;
		SetCommTimeouts(serial_handle.get(), &timeouts);

		DCB dcb{};
		dcb.DCBlength = sizeof(dcb);
		if (!GetCommState(serial_handle.get(), &dcb)) {
			std::cerr << "[Kmbox] GetCommState failed" << std::endl;
			serial_handle.reset();
			return false;
		}

		int baud = baudRate > 0 ? baudRate : 115200;
		dcb.BaudRate = baud;
		dcb.ByteSize = 8;
		dcb.StopBits = ONESTOPBIT;
		dcb.Parity = NOPARITY;
		dcb.fDtrControl = DTR_CONTROL_ENABLE;
		dcb.fRtsControl = RTS_CONTROL_ENABLE;

		if (!SetCommState(serial_handle.get(), &dcb)) {
			std::cerr << "[Kmbox] SetCommState failed" << std::endl;
			serial_handle.reset();
			return false;
		}

		PurgeComm(serial_handle.get(), PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

		// Soft hello — many firmwares answer or ignore
		const char* hi = "km.move(0,0)\r\n";
		DWORD written = 0;
		WriteFile(serial_handle.get(), hi, (DWORD)strlen(hi), &written, nullptr);

		connected = true;
		std::cout << "[Kmbox] Connected OK" << std::endl;
		return true;
	}

	void KmboxInitialize(std::string port)
	{
		// If user passed a COM port, use it; else auto-detect
		std::string use = port;
		// Strip whitespace
		while (!use.empty() && isspace((unsigned char)use.front())) use.erase(use.begin());
		while (!use.empty() && isspace((unsigned char)use.back())) use.pop_back();

		bool looksLikeCom = false;
		{
			std::string u = use;
			for (auto& c : u) c = (char)toupper((unsigned char)c);
			if (u.rfind("COM", 0) == 0 && u.size() >= 4)
				looksLikeCom = true;
		}

		if (!looksLikeCom || use.empty()) {
			std::string autoCom = AutoDetectPort();
			if (autoCom.empty()) {
				std::cerr << "[Kmbox] No COM specified and auto-detect failed" << std::endl;
				connected = false;
				return;
			}
			use = autoCom;
			std::clog << "[DEBUG][Kmbox] Auto-detected " << use << std::endl;
		}

		OpenSerial(use, 115200);
	}

	void KmboxInitializeEx(std::string port, int baud)
	{
		std::string use = port;
		while (!use.empty() && isspace((unsigned char)use.front())) use.erase(use.begin());
		while (!use.empty() && isspace((unsigned char)use.back())) use.pop_back();

		bool looksLikeCom = false;
		{
			std::string u = use;
			for (auto& c : u) c = (char)toupper((unsigned char)c);
			if (u.rfind("COM", 0) == 0 && u.size() >= 4)
				looksLikeCom = true;
		}
		if (!looksLikeCom || use.empty()) {
			std::string autoCom = AutoDetectPort();
			if (autoCom.empty()) {
				connected = false;
				return;
			}
			use = autoCom;
		}
		OpenSerial(use, baud > 0 ? baud : 115200);
	}

	void Disconnect()
	{
		serial_handle.reset();
		connected = false;
	}

	void SendCommand(const std::string& command)
	{
		if (!connected || !serial_handle) return;
		DWORD bytesWritten = 0;
		WriteFile(serial_handle.get(), command.c_str(), (DWORD)command.length(), &bytesWritten, nullptr);
	}

	void move(int x, int y)
	{
		if (!connected) return;
		x = clamp(x);
		y = clamp(y);
		std::string command = "km.move(" + std::to_string(x) + "," + std::to_string(y) + ")\r\n";
		SendCommand(command);
	}

	void left_click()
	{
		if (!connected) return;
		SendCommand("km.left(1)\r\n");
	}

	void left_click_release()
	{
		if (!connected) return;
		SendCommand("km.left(0)\r\n");
	}

	// Physical mouse on the 2nd PC — use local input as bind source.
	// Hardware devices don't report "is button down" over serial for all firmwares.
	bool IsDown(int virtual_key)
	{
		if (virtual_key <= 0) return false;
		return (GetAsyncKeyState(virtual_key) & 0x8000) != 0;
	}

	bool IsKeyJustPressed(int virtual_key)
	{
		static std::map<int, bool> previous_states;
		bool current_state = IsDown(virtual_key);
		bool previous_state = previous_states[virtual_key];
		previous_states[virtual_key] = current_state;
		return current_state && !previous_state;
	}

	bool IsKeyJustReleased(int virtual_key)
	{
		static std::map<int, bool> previous_states;
		bool current_state = IsDown(virtual_key);
		bool previous_state = previous_states[virtual_key];
		previous_states[virtual_key] = current_state;
		return !current_state && previous_state;
	}

	std::vector<std::string> GetAvailableMouseButtons()
	{
		return { "Left Mouse", "Right Mouse", "Middle Mouse", "Mouse 4", "Mouse 5" };
	}

	int GetMouseButtonKeyCode(const std::string& button_name)
	{
		if (button_name == "Left Mouse") return 1;
		if (button_name == "Right Mouse") return 2;
		if (button_name == "Middle Mouse") return 4;
		if (button_name == "Mouse 4") return 5;
		if (button_name == "Mouse 5") return 6;
		return 2;
	}
}
