// dllmain.cpp : Defines the entry point for the DLL application.

#define _CRT_SECURE_NO_WARNINGS

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

#pragma comment(linker, "/export:@16=ogd3d9.#16")
#pragma comment(linker, "/export:@17=ogd3d9.#17")
#pragma comment(linker, "/export:@18=ogd3d9.#18")
#pragma comment(linker, "/export:@19=ogd3d9.#19")
#pragma comment(linker, "/export:Direct3DCreate9On12=ogd3d9.Direct3DCreate9On12,@20")
#pragma comment(linker, "/export:Direct3DCreate9On12Ex=ogd3d9.Direct3DCreate9On12Ex,@21")
#pragma comment(linker, "/export:@22=ogd3d9.#22")
#pragma comment(linker, "/export:@23=ogd3d9.#23")
#pragma comment(linker, "/export:Direct3DShaderValidatorCreate9=ogd3d9.Direct3DShaderValidatorCreate9,@24")
#pragma comment(linker, "/export:PSGPError=ogd3d9.PSGPError,@25")
#pragma comment(linker, "/export:PSGPSampleTexture=ogd3d9.PSGPSampleTexture,@26")
#pragma comment(linker, "/export:D3DPERF_BeginEvent=ogd3d9.D3DPERF_BeginEvent,@27")
#pragma comment(linker, "/export:D3DPERF_EndEvent=ogd3d9.D3DPERF_EndEvent,@28")
#pragma comment(linker, "/export:D3DPERF_GetStatus=ogd3d9.D3DPERF_GetStatus,@29")
#pragma comment(linker, "/export:D3DPERF_QueryRepeatFrame=ogd3d9.D3DPERF_QueryRepeatFrame,@30")
#pragma comment(linker, "/export:D3DPERF_SetMarker=ogd3d9.D3DPERF_SetMarker,@31")
#pragma comment(linker, "/export:D3DPERF_SetOptions=ogd3d9.D3DPERF_SetOptions,@32")
#pragma comment(linker, "/export:D3DPERF_SetRegion=ogd3d9.D3DPERF_SetRegion,@33")
#pragma comment(linker, "/export:DebugSetLevel=ogd3d9.DebugSetLevel,@34")
#pragma comment(linker, "/export:DebugSetMute=ogd3d9.DebugSetMute,@35")
#pragma comment(linker, "/export:Direct3D9EnableMaximizedWindowedModeShim=ogd3d9.Direct3D9EnableMaximizedWindowedModeShim,@36")
#pragma comment(linker, "/export:Direct3DCreate9=ogd3d9.Direct3DCreate9,@37")
#pragma comment(linker, "/export:Direct3DCreate9Ex=ogd3d9.Direct3DCreate9Ex,@38")

int supportedversion = 3788;
int lowestsupportedversion = 3786;

int loadorder = 0;
std::string limitsptr = "?? 48 85 C9 ?? 07 33 D2 E8 ?? DD E8 ?? 48 8D ??";
int* state;
uintptr_t playerinfoptr = 0;

using DisassembleFn = HRESULT(*)(void*, size_t, int, void*, void*);
using AssembleFn = HRESULT(*)(LPCSTR, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, UINT, ID3DBlob**, ID3DBlob**);
using CreateVertexShaderFn = HRESULT(*)(void*, size_t, ID3D11ClassLinkage*, ID3D11VertexShader**);
using D3D11CreateDeviceAndSwapchainFn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
using UpdateSubresourceFn = void(WINAPI*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT);
UpdateSubresourceFn updatesubresource;
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

bool PatchFormationShader(void* bytecode, size_t length, std::vector<uint8_t>& out)
{
	HMODULE compiler = GetModuleHandleA("d3dcompiler_47.dll");
	auto disassemble = (DisassembleFn)GetProcAddress(compiler, "D3DDisassemble");
	auto assemble = (AssembleFn)GetProcAddress(compiler, "D3DAssemble");
	if (!disassemble || !assemble)
	{
		return false;
	}
	ID3DBlob* disasm = nullptr;
	if (FAILED(disassemble(bytecode, length, 0, nullptr, &disasm)) || !disasm)
	{
		return false;
	}
	std::string text = static_cast<const char*>(disasm->GetBufferPointer());
	disasm->Release();
	if (text.find("div r3.w, r3.w, cb11[0].x") == std::string::npos || text.find("min r3.w, r3.w, l(1.000000)") == std::string::npos)
	{
		return false;
	}
	//remove door deformation clamp
	size_t minpos = text.find("min r3.w, r3.w, l(1.000000)");
	if (minpos != std::string::npos)
	{
		text.replace(minpos, strlen("min r3.w, r3.w, l(1.000000)"), "min r3.w, r3.w, l(2147483647.0)");
	}
	//remove wheel deformation limits
	size_t pos = 0;
	while ((pos = text.find("div_sat", pos)) != std::string::npos)
	{
		text.replace(pos, 7, "div    ");
		pos += 7;
	}
	//disable interior intrusion clamping
	size_t lt1 = text.find("lt r3.x, l(0.000000), cb11[2].w");
	if (lt1 != std::string::npos)
	{
		text.replace(lt1, strlen("lt r3.x, l(0.000000), cb11[2].w"), "mov r3.x, l(0)        ");
	}
	size_t lt2 = text.find("lt r3.x, l(0.000000), cb11[3].w");
	if (lt2 != std::string::npos)
	{
		text.replace(lt2, strlen("lt r3.x, l(0.000000), cb11[3].w"), "mov r3.x, l(0)        ");
	}
	ID3DBlob* assembled = nullptr;
	ID3DBlob* errors = nullptr;
	HRESULT hr = assemble(text.c_str(), text.size(), nullptr, nullptr, nullptr, 0, &assembled, &errors);
	if (errors)
	{
		errors->Release();
	}
	if (FAILED(hr) || !assembled)
	{
		return false;
	}
	out.assign((uint8_t*)assembled->GetBufferPointer(), (uint8_t*)assembled->GetBufferPointer() + assembled->GetBufferSize());
	assembled->Release();
	return true;
}

HRESULT HkCreateVertexShader(void* bytecode, size_t size, ID3D11ClassLinkage* linkage, ID3D11VertexShader** shader)
{
	std::vector<uint8_t> patched;
	if (PatchFormationShader(bytecode, size, patched))
	{
		return createvertexshader(patched.data(), patched.size(), linkage, shader);
	}
	loadorder++;
	return createvertexshader(bytecode, size, linkage, shader);
}

void WINAPI HkUpdateSubresource(ID3D11DeviceContext* ctx, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box, const void* data, UINT rowpitch, UINT depthpitch)
{
	if (data && rowpitch == 72)
	{
		float boundradius = *(float*)data;
		float damagemultiplier = *((float*)data + 1);
		if (boundradius > 0.1f && boundradius < 100.0f && damagemultiplier > 0.0f)
		{
			std::vector<uint8_t> patched(72);
			memcpy(patched.data(), data, 72);
			float& br = *((float*)patched.data() + 0);
			float& dm = *((float*)patched.data() + 1);
			br *= 2.0f;
			if (br > 9999999)
			{
				br = 9999999;
			}
			dm *= 120.0f;
			if (dm > 9999999)
			{
				dm = 9999999;
			}
			updatesubresource(ctx, resource, subresource, box, patched.data(), rowpitch, depthpitch);
			return;
		}
	}
	updatesubresource(ctx, resource, subresource, box, data, rowpitch, depthpitch);
}

HRESULT WINAPI HkD3D11CreateDeviceAndSwapchain(IDXGIAdapter* adapter, D3D_DRIVER_TYPE type, HMODULE software, UINT flags, const D3D_FEATURE_LEVEL* featurelevel, UINT featurelevels, UINT sdk, const DXGI_SWAP_CHAIN_DESC* swapchaindesc, IDXGISwapChain** swapchain, ID3D11Device** device, D3D_FEATURE_LEVEL* featurelevel2, ID3D11DeviceContext** devicecontext)
{
	HRESULT dev = d3d11createdeviceandswapchain(adapter, type, software, flags, featurelevel, featurelevels, sdk, swapchaindesc, swapchain, device, featurelevel2, devicecontext);;
	if (dev)
	{
		ID3D11DeviceContext* ctx = nullptr;
		(*device)->GetImmediateContext(&ctx);
		void** ctxvtable = *reinterpret_cast<void***>(ctx);
		MH_CreateHook(ctxvtable[48], &HkUpdateSubresource, reinterpret_cast<void**>(&updatesubresource));
		MH_EnableHook(ctxvtable[48]);
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
	return *reinterpret_cast<T*>(addr);
}

template<typename T>
void Write(uintptr_t addr, T value)
{
	*reinterpret_cast<T*>(addr) = value;
}

uintptr_t Rip(uintptr_t addr)
{
	int32_t rel = *reinterpret_cast<int32_t*>(addr + 3);
	if (rel < 0)
	{
		return addr + 7 + static_cast<uint32_t>(rel);
	}
	return addr + 7 + rel;
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
	}
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

