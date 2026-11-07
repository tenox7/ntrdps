/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <dderror.h>
#include <devioctl.h>
#include <miniport.h>
#include <ntddvdeo.h>
#include <video.h>

typedef struct MPEXT {
    ULONG cRequests;
} MPEXT;

static ULONG g_devices = 1;

ULONG DriverEntry(PVOID Context1, PVOID Context2);

static VP_STATUS HwFindAdapter(PVOID HwDeviceExtension, PVOID HwContext,
    PWSTR ArgumentString, PVIDEO_PORT_CONFIG_INFO ConfigInfo,
    PUCHAR Again);
static BOOLEAN HwInitialize(PVOID HwDeviceExtension);
static BOOLEAN HwStartIO(PVOID HwDeviceExtension,
    PVIDEO_REQUEST_PACKET RequestPacket);

ULONG DriverEntry(PVOID Context1, PVOID Context2)
{
    static const INTERFACE_TYPE buses[] = {
        Isa, Eisa, MicroChannel, Internal, PCIBus
    };
    VIDEO_HW_INITIALIZATION_DATA init;
    ULONG status;
    ULONG best = (ULONG) -1;
    ULONG i;

    for (i = 0; i < sizeof(buses) / sizeof(buses[0]); i++) {
        VideoPortZeroMemory(&init, sizeof(init));
        init.HwInitDataSize = sizeof(VIDEO_HW_INITIALIZATION_DATA);
        init.HwFindAdapter = HwFindAdapter;
        init.HwInitialize = HwInitialize;
        init.HwStartIO = HwStartIO;
        init.HwDeviceExtensionSize = sizeof(MPEXT);
        init.AdapterInterfaceType = buses[i];
        status = VideoPortInitialize(Context1, Context2, &init, &g_devices);
        if (status < best) {
            best = status;
        }
    }
    return best;
}

static VP_STATUS HwFindAdapter(PVOID HwDeviceExtension, PVOID HwContext,
    PWSTR ArgumentString, PVIDEO_PORT_CONFIG_INFO ConfigInfo,
    PUCHAR Again)
{
    PULONG left = (PULONG) HwContext;

    *Again = 0;
    if (ConfigInfo->Length < sizeof(VIDEO_PORT_CONFIG_INFO)) {
        return ERROR_INVALID_PARAMETER;
    }
    if (left == NULL || *left == 0) {
        return ERROR_DEV_NOT_EXIST;
    }
    *left -= 1;

    VideoPortVerifyAccessRanges(HwDeviceExtension, 0, NULL);
    ConfigInfo->BusInterruptLevel = 0;
    ConfigInfo->BusInterruptVector = 0;
    ConfigInfo->NumEmulatorAccessEntries = 0;
    ConfigInfo->EmulatorAccessEntries = NULL;
    ConfigInfo->EmulatorAccessEntriesContext = 0;
    ConfigInfo->HardwareStateSize = 0;
    ConfigInfo->VdmPhysicalVideoMemoryAddress.LowPart = 0;
    ConfigInfo->VdmPhysicalVideoMemoryAddress.HighPart = 0;
    ConfigInfo->VdmPhysicalVideoMemoryLength = 0;
    return NO_ERROR;
}

static BOOLEAN HwInitialize(PVOID HwDeviceExtension)
{
    return TRUE;
}

static BOOLEAN HwStartIO(PVOID HwDeviceExtension,
    PVIDEO_REQUEST_PACKET RequestPacket)
{
    ((MPEXT *) HwDeviceExtension)->cRequests++;
    RequestPacket->StatusBlock->Information = 0;
    switch (RequestPacket->IoControlCode) {
    case IOCTL_VIDEO_RESET_DEVICE:
    case IOCTL_VIDEO_SET_CURRENT_MODE:
    case IOCTL_VIDEO_SET_COLOR_REGISTERS:
        RequestPacket->StatusBlock->Status = NO_ERROR;
        break;
    default:
        RequestPacket->StatusBlock->Status = ERROR_INVALID_FUNCTION;
        break;
    }
    return TRUE;
}
