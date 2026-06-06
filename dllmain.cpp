// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <vector>
#include <stdint.h>
#include <MinHook.h>
#include <sstream>
#include <atomic>
#include <winver.h>
#include <fstream>
#include <chrono>
#include <iomanip>
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "Version.lib")

#pragma comment(linker, "/export:D3DPERF_BeginEvent=ogd3d9.D3DPERF_BeginEvent,@1")
#pragma comment(linker, "/export:D3DPERF_EndEvent=ogd3d9.D3DPERF_EndEvent,@2")
#pragma comment(linker, "/export:D3DPERF_GetStatus=ogd3d9.D3DPERF_GetStatus,@3")
#pragma comment(linker, "/export:D3DPERF_QueryRepeatFrame=ogd3d9.D3DPERF_QueryRepeatFrame,@4")
#pragma comment(linker, "/export:D3DPERF_SetMarker=ogd3d9.D3DPERF_SetMarker,@5")
#pragma comment(linker, "/export:D3DPERF_SetOptions=ogd3d9.D3DPERF_SetOptions,@6")
#pragma comment(linker, "/export:D3DPERF_SetRegion=ogd3d9.D3DPERF_SetRegion,@7")
#pragma comment(linker, "/export:DebugSetLevel=ogd3d9.DebugSetLevel,@8")
#pragma comment(linker, "/export:DebugSetMute=ogd3d9.DebugSetMute,@9")
#pragma comment(linker, "/export:Direct3D9EnableMaximizedWindowedModeShim=ogd3d9.Direct3D9EnableMaximizedWindowedModeShim,@10")
#pragma comment(linker, "/export:Direct3DCreate9=ogd3d9.Direct3DCreate9,@11")
#pragma comment(linker, "/export:Direct3DCreate9Ex=ogd3d9.Direct3DCreate9Ex,@12")
#pragma comment(linker, "/export:Direct3DCreate9On12=ogd3d9.Direct3DCreate9On12,@13")
#pragma comment(linker, "/export:Direct3DCreate9On12Ex=ogd3d9.Direct3DCreate9On12Ex,@16")
#pragma comment(linker, "/export:Direct3DShaderValidatorCreate9=ogd3d9.Direct3DShaderValidatorCreate9,@17")
#pragma comment(linker, "/export:PSGPError=ogd3d9.PSGPError,@18")
#pragma comment(linker, "/export:PSGPSampleTexture=ogd3d9.PSGPSampleTexture,@19")

int supportedversion = 3788;
int lowestsupportedversion = 3786;

int loadorder = 0;
std::string limitsptr = "?? 48 85 C9 ?? 07 33 D2 E8 ?? DD E8 ?? 48 8D ??";
std::string worldptr = "48 8B ?? F7 7E 68 ?? 48 ?? 58 08 ?? 85 DB 74 ??";
std::string stateptr = "81 39 ?? 6D FF AF ?? 20 83 3D 35 ?? DA ?? 05 75 17 ?? 42";
int* state;
uintptr_t playerinfoptr = 0;
std::atomic<bool> run = true;
uintptr_t lastvehicle = 0;

using DisassembleFn = HRESULT(*)(void*, size_t, int, void*, void*);
using CreateVertexShaderFn = HRESULT(*)(void*, size_t, ID3D11ClassLinkage*, ID3D11VertexShader**);
using D3D11CreateDeviceAndSwapchainFn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
CreateVertexShaderFn createvertexshader;
D3D11CreateDeviceAndSwapchainFn d3d11createdeviceandswapchain;

void AppendToLogFile(const char* fmt, ...)
{
	static std::ofstream file("LowerDamageLimits.log", std::ios::app);
	char buffer[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	auto now = std::chrono::system_clock::now();
	auto time = std::chrono::system_clock::to_time_t(now);
	std::tm tm{};
	localtime_s(&tm, &time);
	file << '[' << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] " << buffer << std::endl;
}

bool PatchFormationShader(void* bytecode, size_t length)
{
	ID3DBlob* disasm = nullptr;
	DisassembleFn disassemble = (DisassembleFn)GetProcAddress(GetModuleHandleA("d3dcompiler_47.dll"), "D3DDisassemble");
	if (FAILED(disassemble(bytecode, length, 0, nullptr, &disasm)))
	{
		return false;
	}
	const char* text = static_cast<const char*>(disasm->GetBufferPointer());
	const char* pattern = "div r3.w, r3.w, cb11[0].x\n  min r3.w, r3.w, l(1.000000)";
	const char* found = strstr(text, pattern);
	disasm->Release();
	if (!found)
	{
		return false;
	}
	uint8_t* data = (uint8_t*)bytecode;
	for (size_t i = 0; i <= length - 16; i += 4)
	{
		if (*(uint32_t*)(data + i) == 0x00000003 && *(uint32_t*)(data + i + 4) == 0x00004001 && *(uint32_t*)(data + i + 8) == 0x3F800000 && *(uint32_t*)(data + i + 12) == 0x07000038)
		{
			*(uint32_t*)(data + i + 8) = 0x4f000000;
			return true;
		}
	}
	return false;
}

HRESULT HkCreateVertexShader(void* bytecode, size_t size, ID3D11ClassLinkage* linkage, ID3D11VertexShader** shader)
{
	std::vector<uint8_t> patched((uint8_t*)bytecode, (uint8_t*)bytecode + size);
	if (PatchFormationShader(patched.data(), patched.size()))
	{
		return createvertexshader(patched.data(), patched.size(), linkage, shader);
	}
	loadorder++;
	return createvertexshader(bytecode, size, linkage, shader);
}

HRESULT WINAPI HkD3D11CreateDeviceAndSwapchain(IDXGIAdapter* adapter, D3D_DRIVER_TYPE type, HMODULE software, UINT flags, const D3D_FEATURE_LEVEL* featurelevel, UINT featurelevels, UINT sdk, const DXGI_SWAP_CHAIN_DESC* swapchaindesc, IDXGISwapChain** swapchain, ID3D11Device** device, D3D_FEATURE_LEVEL* featurelevel2, ID3D11DeviceContext** devicecontext)
{
	HRESULT dev = d3d11createdeviceandswapchain(adapter, type, software, flags, featurelevel, featurelevels, sdk, swapchaindesc, swapchain, device, featurelevel2, devicecontext);;
	if (dev)
	{
		void** vtable = *reinterpret_cast<void***>(dev);
		MH_CreateHook(vtable[12], HkCreateVertexShader, reinterpret_cast<void**>(&createvertexshader));
		MH_EnableHook(vtable[12]);
	}
	return dev;
}

bool IsGTA5()
{
	const std::wstring& gta = L"GTA5.exe";
	wchar_t path[MAX_PATH];
	if (GetModuleFileNameW(NULL, path, MAX_PATH) == 0)
	{
		return false;
	}
	std::wstring filename = path;
	size_t pos = filename.find_last_of(L"\\/");
	if (pos != std::wstring::npos)
	{
		filename = filename.substr(pos + 1);
	}
	return _wcsicmp(filename.c_str(), gta.c_str()) == 0;
}

bool ParseAOB(const std::string& pattern, std::vector<uint8_t>& bytes, std::string& mask)
{
	std::istringstream iss(pattern);
	std::string s;
	while (iss >> s)
	{
		if (s == "?" || s == "??")
		{
			bytes.push_back(0);
			mask.push_back('?');
		}
		else
		{
			bytes.push_back(static_cast<uint8_t>(std::strtoul(s.c_str(), nullptr, 16)));
			mask.push_back('x');
		}
	}
	return !bytes.empty();
}

uintptr_t FindInRegion(uintptr_t start, uintptr_t size, const std::vector<uint8_t>& aob, const std::string& mask)
{
	size_t aobsize = aob.size();
	if (aobsize == 0 || size < aobsize)
	{
		return 0;
	}
	uint8_t* region = reinterpret_cast<uint8_t*>(start);
	uint8_t* end = region + size - aobsize;
	for (uint8_t* p = region; p <= end; ++p)
	{
		bool found = true;
		for (size_t i = 0; i < aobsize; ++i)
		{
			if (mask[i] == 'x' && p[i] != aob[i])
			{
				found = false;
				break;
			}
		}
		if (found)
		{
			return reinterpret_cast<uintptr_t>(p);
		}
	}
	return 0;
}

uintptr_t GetAddress(HMODULE module, const std::string& aobstr)
{
	std::vector<uint8_t> pattern;
	std::string mask;
	if (!ParseAOB(aobstr, pattern, mask))
	{
		return 0;
	}
	auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
	auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>((uint8_t*)module + dos->e_lfanew);
	auto sections = IMAGE_FIRST_SECTION(nt);
	for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
	{
		auto& sec = sections[i];
		if (!(sec.Characteristics & IMAGE_SCN_MEM_EXECUTE))
		{
			continue;
		}
		uintptr_t start = reinterpret_cast<uintptr_t>(module) + sec.VirtualAddress;
		size_t size = sec.Misc.VirtualSize;
		uintptr_t current = start;
		uintptr_t end = start + size;
		while (current < end)
		{
			MEMORY_BASIC_INFORMATION mbi{};
			if (!VirtualQuery(reinterpret_cast<void*>(current), &mbi, sizeof(mbi)))
			{
				break;
			}
			bool executable = (mbi.Protect & PAGE_EXECUTE) || (mbi.Protect & PAGE_EXECUTE_READ) || (mbi.Protect & PAGE_EXECUTE_READWRITE) || (mbi.Protect & PAGE_EXECUTE_WRITECOPY);
			if (mbi.State == MEM_COMMIT && executable)
			{
				uintptr_t scanstart = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
				uintptr_t scansize = mbi.RegionSize;
				if (scanstart + scansize > end)
				{
					scansize = end - scanstart;
				}
				uintptr_t found = FindInRegion(scanstart, scansize, pattern, mask);
				if (found)
				{
					return found;
				}
			}
			current += mbi.RegionSize;
		}
	}
	return 0;
}

uintptr_t ResolveRel32(uintptr_t addr)
{
	int32_t rel = *reinterpret_cast<int32_t*>(addr);
	return addr + 4 + rel;
}

int GetBuildNumber(const char* path)
{
	DWORD dummy = 0;
	DWORD size = GetFileVersionInfoSizeA(path, &dummy);
	if (!size)
	{
		return 0;
	}
	std::vector<BYTE> data(size);
	if (!GetFileVersionInfoA(path, 0, size, data.data()))
	{
		return 0;
	}
	VS_FIXEDFILEINFO* ffi = nullptr;
	UINT len = 0;
	if (!VerQueryValueA(data.data(), "\\", (LPVOID*)&ffi, &len))
	{
		return 0;
	}
	DWORD major = HIWORD(ffi->dwFileVersionMS);
	DWORD minor = LOWORD(ffi->dwFileVersionMS);
	DWORD build = HIWORD(ffi->dwFileVersionLS);
	DWORD revision = LOWORD(ffi->dwFileVersionLS);
	return static_cast<int>(build);
}

template<typename T>
T Read(uintptr_t addr)
{
	if (!addr || addr < 0x10000)
	{
		return (T)0x0;
	}
	if (IsBadReadPtr(reinterpret_cast<void*>(addr), sizeof(T)))
	{
		return (T)0x0;
	}
	return *reinterpret_cast<T*>(addr);
}

template<typename T>
void Write(uintptr_t addr, T value)
{
	*reinterpret_cast<T*>(addr) = value;
}

DWORD WINAPI InitializeD3D(LPVOID)
{
	if (!IsGTA5())
	{
		return 0;
	}
	while (!GetModuleHandleA("d3d11.dll"))
	{
		Sleep(10);
	}
	HMODULE d3d = GetModuleHandleA("d3d11.dll");
	HMODULE gta = GetModuleHandleA("GTA5.exe");
	int build = GetBuildNumber("GTA5.exe");
	if (d3d)
	{
		FARPROC func = GetProcAddress(d3d, "D3D11CreateDeviceAndSwapChain");
		MH_CreateHook((LPVOID)func, &HkD3D11CreateDeviceAndSwapchain, reinterpret_cast<LPVOID*>(&d3d11createdeviceandswapchain));
		MH_EnableHook((LPVOID)func);
		AppendToLogFile("Hooked D3D11CreateDeviceAndSwapChain.");
		if (build < lowestsupportedversion || build > supportedversion)
		{
			AppendToLogFile("Unknown/Incompatible game version. Some aspects of the mod have been disabled for compatibility, main functionality of the mod is still active.");
			AppendToLogFile("If your game still doesn't boot you may have broken/invalid shaders, if so, verify the integrity of your game files.");
			return 0;
		};
		uint8_t* ignorelimits = reinterpret_cast<uint8_t*>(GetAddress(gta, limitsptr));
		DWORD old;
		VirtualProtect(ignorelimits, 1, PAGE_EXECUTE_READWRITE, &old);
		*ignorelimits = 1;
		VirtualProtect(ignorelimits, 1, old, &old);
		AppendToLogFile("Patched out flag.");
		Sleep(3000);
		uintptr_t stateaddr = GetAddress(gta, stateptr);
		state = reinterpret_cast<int*>(ResolveRel32((stateaddr + 10)) + 1);
		AppendToLogFile("Found game state.");
	}
	do
	{
		Sleep(500);
	} while (*state != 0);
	uintptr_t world = GetAddress(gta, worldptr);
	uintptr_t me = Read<INT64>(world + 0x8);
	playerinfoptr = Read<INT64>(me + 0x10A8);
	AppendToLogFile("Found CPlayerInfo.");
	do
	{
		Sleep(500);
		uintptr_t vehicle = Read<INT64>(me + 0xD10);
		if (!vehicle || vehicle < 0x10000)
		{
			continue;
		}
		if (vehicle == lastvehicle)
		{
			continue;
		}
		lastvehicle = vehicle;
		uintptr_t handling = Read<INT64>(vehicle + 0x960);
		if (!handling || handling < 0x10000)
		{
			continue;
		}
		float deformmult = Read<FLOAT>(handling + 0xF8);
		Write<FLOAT>(handling + 0xF8, deformmult + 1.0);
	} while (run);
	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
		DisableThreadLibraryCalls(hModule);
		HANDLE d3dthread = CreateThread(nullptr, 0, InitializeD3D, nullptr, 0, nullptr);
		if (d3dthread)
		{
			CloseHandle(d3dthread);
		}
		break;
	}
	case DLL_PROCESS_DETACH:
		MH_DisableHook(MH_ALL_HOOKS);
		MH_RemoveHook(MH_ALL_HOOKS);
		MH_Uninitialize();
		break;
	}
	return TRUE;
}

