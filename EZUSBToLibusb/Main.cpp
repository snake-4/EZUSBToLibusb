#include <Windows.h>
#include <detours/detours.h>
#include <libusb-1.0/libusb.h>
#include <optional>
#include <utility>
#include "Hooks.hpp"
#include "Globals.hpp"
#include "Config.hpp"
#include "Utils.hpp"
#include "EZUSB/ezusb.hpp"

GHandleManager_t GHandleManager;
libusb_context* GLibUsbCtx = nullptr;
std::optional<LIBUSBDevice> GUSBDev;
Configuration_t GConfig = Configuration_t::LoadFromJsonFile("eu2lu_config.json");
HANDLE GUSBInitThread = INVALID_HANDLE_VALUE;

DWORD WINAPI USBInitThread(LPVOID lpParameter)
{
	/*
	* libusb_init calls LoadLibrary, which is not allowed in DllMain
	* and does result in a crash on my up-to-date Windows 7 VM
	* which is why we do it in a separate thread.
	* Besides, it makes sense to search for the USB devices in the background, as the process may take long.
	*/
	libusb_init(&GLibUsbCtx);

	for (const auto& entry : GConfig.USBSearchList)
	{
		auto dev = LIBUSBDevice::OpenDevice(entry.VID, entry.PID, GConfig.USBTimeoutMS,
			EZUSB::USB_CONFIG_INDEX, EZUSB::USB_INTERFACE_INDEX, EZUSB::USB_INTERFACE_ALTERNATE_SETTING);

		if (dev.has_value()) {
			GUSBDev = dev;
			break;
		}
	}
	return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved)
{
	UNREFERENCED_PARAMETER(hinst);
	UNREFERENCED_PARAMETER(reserved);

	if (DetourIsHelperProcess())
	{
		return TRUE;
	}

	if (dwReason == DLL_PROCESS_ATTACH)
	{
		DetourRestoreAfterWith();

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		AttachHooks();
		DetourTransactionCommit();

		if (GConfig.Debug > 0) {
			AllocConsole();
			BindCrtHandlesToStdHandles(true, true, true);
			_putenv("LIBUSB_DEBUG=4");
		}

		GUSBInitThread = CreateThread(NULL, 0, USBInitThread, NULL, 0, NULL);
	}
	else if (dwReason == DLL_PROCESS_DETACH)
	{
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetachHooks();
		DetourTransactionCommit();

		CloseHandle(GUSBInitThread);

		//libusb_exit must be called after all USB objects are destructured
		//libusb_exit(GLibUsbCtx);
	}
	return TRUE;
}
