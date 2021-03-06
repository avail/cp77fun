#define _CRT_SECURE_NO_WARNINGS 1

#include <Windows.h>
#include <map>
#include <vector>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <iterator>
#include <strsafe.h>
#include <filesystem>

#include "Hooking.Patterns.h"
#include <MinHook.h>
#include <injector/injector.hpp>
#include "Utils.h"
#include "Game.h"

#define LOGGER_NAME "Experiments"
#include <logging.h>

static void(__cdecl* Orig_SetFeatureDefault)(void* vt, const char* overlayName, int enabled);
void __fastcall Hook_SetFeatureDefault(void* vt, const char* overlayName, int enabled)
{
	logger::write("Forcing feature: %s", overlayName);
	Orig_SetFeatureDefault(vt, overlayName, true);
}

static int(__cdecl* Orig_FeatureCtor)(__int64 a1, const char* category, const char* value, char a4, char a5);
int __fastcall Hook_FeatureCtor(__int64 a1, const char* category, const char* value, char a4, char a5)
{
	logger::write(L"ctor: %s -> %s (%d, %d)", utils::to_wide(category).c_str(), utils::to_wide(value).c_str(), a4, a5);
	return Orig_FeatureCtor(a1, category, value, a4, a5);
}

static uint16_t(__cdecl* Orig_FeatureCtor2)(__int64 a1, const char* category);
uint16_t __fastcall Hook_FeatureCtor2(__int64 a1, const char* category)
{
	logger::write(L"ctor: %s -> %s (%d, %d)", utils::to_wide(category).c_str());
	return Orig_FeatureCtor2(a1, category);
}

FILE* hashes;

static __int64(__cdecl* Orig_RegisterHashString)(__int64 a1, const char* str);
__int64 __fastcall Hook_RegisterHashString(__int64 a1, const char* str)
{
	fprintf(hashes, "{ \"%s\", 0x%llx },\n", str, a1);
	fflush(hashes);
	return Orig_RegisterHashString(a1, str);
}

static __int64(__cdecl* Orig_SomeLogFunc)(__int64 a1, const char** a2);
__int64 Hook_SomeLogFunc(__int64 a1, const char** a2)
{
	logger::write("%s", *a2);
	return Orig_SomeLogFunc(a1, a2);
}

static void(__cdecl* Orig_LoadArchive)(void* a1, __int64* a2);
void Hook_LoadArchive(void* a1, __int64* a2)
{
	auto an = GetArchiveName(a2);
	logger::write("Loading archive %s", an);
	Orig_LoadArchive(a1, a2);
}

FILE* commands;

static __int64(__cdecl* Orig_SetCommandCategory)(__int64 a1, const char* category);
__int64 Hook_SetCommandCategory(__int64 a1, const char* category)
{
	fprintf(commands, "%s\n", category);
	fflush(commands);
	return Orig_SetCommandCategory(a1, category);
}

static __int64(__cdecl* Orig_AddCommand)(__int64 a1, __int64 a2, __int64 hash, __int64 a4, __int64 a5, int a6);
__int64 Hook_AddCommand(__int64 a1, __int64 a2, __int64 hash, __int64 a4, __int64 a5, int a6)
{
	auto hashStr = Hash2String(&hash);
	fprintf(commands, "\t%s\n", hashStr);
	fflush(commands);
	return Orig_AddCommand(a1, a2, hash, a4, a5, a6);
}

// Game.cpp
void AddGameFunctions();

DWORD WINAPI OnAttach(LPVOID lpParameter)
{
	logger::write("[+] OnAttach");

	AddGameFunctions();

	hashes = fopen("hashes.txt", "w");
	commands = fopen("commands.txt", "w");

	hook::set_base((uintptr_t)GetModuleHandle(nullptr));

	MH_Initialize();
	//MH_CreateHook(hook::get_pattern("E8 ? ? ? ? 48 85 C0 74 ? 0F B7 10", -0x14), Hook_SetFeatureDefault, (void**)&Orig_SetFeatureDefault);
	MH_CreateHook(hook::get_pattern("4C 89 41 08 48 89 01 33 C0 48 89 41 18", -0x07), Hook_FeatureCtor, (void**)&Orig_FeatureCtor);
	MH_CreateHook(hook::pattern("48 8D ? ? ? ? ? 4C 89 41 08 48 89 01 33").count(2).get(1).get<void*>(), Hook_FeatureCtor2, (void**)&Orig_FeatureCtor2);
	MH_CreateHook(hook::get_pattern("48 83 EC 38 33 C0 48 89 54 24 20"), Hook_RegisterHashString, (void**)&Orig_RegisterHashString);
	MH_CreateHook(hook::get_pattern("48 83 EC 38 33 C0 48 89 54 24 20"), Hook_RegisterHashString, (void**)&Orig_RegisterHashString);
	//MH_CreateHook(hook::get_pattern("48 89 5C 24 08 57 48 83 EC 20 41 B9 40"), Hook_SomeLogFunc, (void**)Orig_SomeLogFunc);
	MH_CreateHook(hook::get_pattern("48 8D 54 24 20 E8 ? ? ? ? EB 05", -0x47), Hook_LoadArchive, (void**)&Orig_LoadArchive);

	MH_CreateHook((void*)hook::get_adjusted(0x1401BFF90), Hook_SetCommandCategory, (void**)&Orig_SetCommandCategory);
	MH_CreateHook((void*)hook::get_adjusted(0x140224D20), Hook_AddCommand, (void**)&Orig_AddCommand);

	if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
	{
		logger::write("[*] OnAttach: failed hooking");
	}

	// enable debug menu
	//if (false)
	{
		auto patterns = hook::pattern("48 FF 02 4D 85 C0 74 04");
		if (patterns.size() == 2)
		{
			// IsFinal
			injector::WriteMemory<BYTE>(patterns.get(0).get<char>(0) + 11, false, true);

			// UseProfiler
			injector::WriteMemory<BYTE>(patterns.get(1).get<char>(0) + 11, true, true);
		}
	}

	injector::MakeNOP(hook::get_pattern("48 8B CF E8 ? ? ? ? 80 7E 5D 00", 12), 6); // overlay
	injector::MakeNOP(hook::get_pattern("80 7E 5E 00 74 ? 41 B0 01", 5), 2); // versionstr

	if (false)
	{
		auto locations = hook::pattern("84 C0 74 ? 48 8D 4C 24 20 E8 ? ? ? ? 84 C0 75 ?");
		injector::MakeNOP(locations.get(0).get<char>(2), 2); // dbg #1
		injector::MakeNOP(locations.get(0).get<char>(16), 2); // dbg #2
	}

	// skip crc hash verification while loading archives (found by Snazz#3248)
	//injector::WriteMemory<BYTE>(hook::get_pattern("8B F8 85 C0 74 36 48 8B CB", 4), 0xEB, true); // file validation, we don't want to bypass this one
	injector::WriteMemory<BYTE>(hook::get_pattern("48 3B C1 74 0A B8 03 00 00 00 E9 6C 03 00 00", 2), 0xC0, true);
	injector::WriteMemory<BYTE>(hook::get_pattern("48 3B C1 74 0A B8 03 00 00 00 E9 A2 01 00 00", 2), 0xC0, true);
	
	//injector::MakeNOP(hook::get_adjusted(0x140AC82F7), 6); // DrawDebugOverlay
	//injector::MakeNOP(hook::get_adjusted(0x140AD3EE6), 6); // imgui?

	// SetDebugOverlayVisibility
	//injector::WriteObject<uint64_t>(hook::get_adjusted(0x143812248), 1, true);

	logger::write("[-] OnAttach");

	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		freopen("CON", "w", stdout);
		freopen("CONIN$", "r", stdin);

		OnAttach(hModule);
		//CreateThread(nullptr, 0, OnAttach, hModule, 0, nullptr);
	}

	if (ul_reason_for_call == DLL_PROCESS_DETACH)
	{
		MH_DisableHook(MH_ALL_HOOKS);
		fclose(hashes);
		fclose(commands);
	}

	return true;
}