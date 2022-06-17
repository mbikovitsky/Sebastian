#include "Sebastian.h"
#include "Sebastian_Internal.h"

#include <ntifs.h>
#include <wmilib.h>
#include <wmistr.h>

#include "Common.h"


STATIC UNICODE_STRING CONST g_usSmbiosWmiDeviceName =
    RTL_CONSTANT_STRING(L"\\Device\\WMIDataDevice");

// {8F680850-A584-11d1-BF38-00A0C9062910}
STATIC GUID CONST g_tMSSMBios_RawSMBiosTables =
    {0x8F680850, 0xA584, 0x11d1, {0xBF, 0x38, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0x10}};

// {9831b7e6-09ac-491f-8d07-3c3d649d8240}
STATIC GUID CONST g_tWmiMonitorBasicDisplayParamsGuid =
    {0x9831b7e6, 0x09ac, 0x491f, {0x8d, 0x07, 0x3c, 0x3d, 0x64, 0x9d, 0x82, 0x40}};

STATIC UNICODE_STRING g_usRegistryPath = {0};

STATIC UNICODE_STRING g_usDisplayWmiBaseName = {0};

STATIC PDEVICE_OBJECT g_ptWmiDevice = NULL;

STATIC PDRIVER_DISPATCH g_pfnOriginalSmbiosWmiDispatch = NULL;


#pragma alloc_text("PAGE", SEBASTIAN_Initialize)
_Use_decl_annotations_
NTSTATUS
SEBASTIAN_Initialize(
    PUNICODE_STRING pusRegistryPath
)
{
    NTSTATUS       eStatus              = STATUS_UNSUCCESSFUL;
    UNICODE_STRING usRegistryPath       = {0};
    UNICODE_STRING usDisplayWmiBaseName = {0};

    PAGED_CODE();
    NT_ASSERT(PASSIVE_LEVEL == KeGetCurrentIrql());

    if (NULL == pusRegistryPath)
    {
        eStatus = STATUS_INVALID_PARAMETER;
        goto lblCleanup;
    }

    if (NULL != g_ptWmiDevice
        || NULL != g_pfnOriginalSmbiosWmiDispatch
        || NULL != g_usRegistryPath.Buffer
        || NULL != g_usDisplayWmiBaseName.Buffer)
    {
        eStatus = STATUS_ALREADY_INITIALIZED;
        goto lblCleanup;
    }

    usRegistryPath.Length        = pusRegistryPath->Length;
    usRegistryPath.MaximumLength = pusRegistryPath->Length;
    usRegistryPath.Buffer =
        ExAllocatePoolWithTag(NonPagedPoolNx, pusRegistryPath->Length, SEBASTIAN_POOL_TAG);
    if (NULL == usRegistryPath.Buffer)
    {
        eStatus = STATUS_INSUFFICIENT_RESOURCES;
        goto lblCleanup;
    }
    RtlMoveMemory(usRegistryPath.Buffer, pusRegistryPath->Buffer, pusRegistryPath->Length);

    eStatus = sebastian_GetDisplayWmiBaseName(pusRegistryPath, &usDisplayWmiBaseName);
    if (!NT_SUCCESS(eStatus))
    {
        goto lblCleanup;
    }

    // Transfer ownership:
    g_usRegistryPath            = usRegistryPath;
    usRegistryPath.Buffer       = NULL;
    g_usDisplayWmiBaseName      = usDisplayWmiBaseName;
    usDisplayWmiBaseName.Buffer = NULL;

    eStatus = STATUS_SUCCESS;

lblCleanup:
    CLOSE(usDisplayWmiBaseName.Buffer, ExFreePool);
    CLOSE(usRegistryPath.Buffer, ExFreePool);

    return eStatus;
}

#pragma alloc_text("PAGE", SEBASTIAN_Uninitialize)
_Use_decl_annotations_
VOID
SEBASTIAN_Uninitialize(VOID)
{
    PAGED_CODE();
    NT_ASSERT(PASSIVE_LEVEL == KeGetCurrentIrql());

    if (NULL != g_ptWmiDevice || NULL != g_pfnOriginalSmbiosWmiDispatch)
    {
        goto lblCleanup;
    }

    CLOSE(g_usRegistryPath.Buffer, ExFreePool);
    RtlZeroMemory(&g_usRegistryPath, sizeof(g_usRegistryPath));

    CLOSE(g_usDisplayWmiBaseName.Buffer, ExFreePool);
    RtlZeroMemory(&g_usDisplayWmiBaseName, sizeof(g_usDisplayWmiBaseName));

lblCleanup:
    return;
}

#pragma alloc_text("PAGE", SEBASTIAN_Hook)
_Use_decl_annotations_
NTSTATUS
SEBASTIAN_Hook(
    PDEVICE_OBJECT  ptWmiDevice
)
{
    NTSTATUS       eStatus           = STATUS_UNSUCCESSFUL;
    PFILE_OBJECT   ptFileObject      = NULL;
    PDEVICE_OBJECT ptSmbiosWmiDevice = NULL;

    PAGED_CODE();
    NT_ASSERT(PASSIVE_LEVEL == KeGetCurrentIrql());

    if (NULL == ptWmiDevice)
    {
        eStatus = STATUS_INVALID_PARAMETER;
        goto lblCleanup;
    }

    if (NULL == g_usRegistryPath.Buffer
        || NULL == g_usDisplayWmiBaseName.Buffer
        || NULL != g_ptWmiDevice
        || NULL != g_pfnOriginalSmbiosWmiDispatch)
    {
        eStatus = STATUS_INVALID_DEVICE_STATE;
        goto lblCleanup;
    }

    eStatus = IoGetDeviceObjectPointer((PUNICODE_STRING)&g_usSmbiosWmiDeviceName,
                                       FILE_ALL_ACCESS,
                                       &ptFileObject,
                                       &ptSmbiosWmiDevice);
    if (!NT_SUCCESS(eStatus))
    {
        goto lblCleanup;
    }

    g_ptWmiDevice = ptWmiDevice;

    eStatus = IoWMIRegistrationControl(g_ptWmiDevice, WMIREG_ACTION_REGISTER);
    if (!NT_SUCCESS(eStatus))
    {
        g_ptWmiDevice = NULL;
        goto lblCleanup;
    }

    // NO FAILURE PAST THIS POINT!
    // The current implementation does not support unhooking.

#pragma warning(push)
#pragma warning(disable: 28175) // The 'MajorFunction' member of _DRIVER_OBJECT should not be accessed by a driver
    g_pfnOriginalSmbiosWmiDispatch =
        ptSmbiosWmiDevice->DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL];
    MemoryBarrier();

    InterlockedExchangePointer(
        (PVOID *)&ptSmbiosWmiDevice->DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL],
        (PVOID)&sebastian_SmbiosWmiDispatch
    );
#pragma warning(pop)

    eStatus = STATUS_SUCCESS;

lblCleanup:
    DEREFERENCE_OBJECT(ptFileObject);

    return eStatus;
}

#pragma alloc_text("PAGE", SEBASTIAN_WmiDispatch)
_Use_decl_annotations_
NTSTATUS
SEBASTIAN_WmiDispatch(
    PDEVICE_OBJECT  ptDeviceObject,
    PIRP            ptIrp
)
{
    NTSTATUS               eStatus      = STATUS_UNSUCCESSFUL;
    WMIGUIDREGINFO         tRegInfo     = {0};
    WMILIB_CONTEXT         tContext     = {0};
    SYSCTL_IRP_DISPOSITION eDisposition = IrpNotCompleted;

    PAGED_CODE();
    NT_ASSERT(PASSIVE_LEVEL == KeGetCurrentIrql());

    NT_ASSERT(NULL != ptDeviceObject);
    NT_ASSERT(NULL != ptIrp);

    if (ptDeviceObject != g_ptWmiDevice)
    {
        NT_ASSERT(FALSE); // This really shouldn't happen

        eStatus                     = STATUS_INVALID_DEVICE_OBJECT_PARAMETER;
        ptIrp->IoStatus.Status      = eStatus;
        ptIrp->IoStatus.Information = 0;
        COMPLETE_IRP(ptIrp, IO_NO_INCREMENT);
        goto lblCleanup;
    }

    tRegInfo.Guid          = &g_tWmiMonitorBasicDisplayParamsGuid;
    tRegInfo.InstanceCount = 1;

    tContext.GuidCount         = 1;
    tContext.GuidList          = &tRegInfo;
    tContext.QueryWmiRegInfo   = &sebastian_WmiQueryRegInfo;
    tContext.QueryWmiDataBlock = &sebastian_WmiQueryDataBlock;

    // https://www.osronline.com/article.cfm%5Eid=95.htm

    eStatus = WmiSystemControl(&tContext, ptDeviceObject, ptIrp, &eDisposition);
    switch (eDisposition)
    {
    case IrpProcessed:
        ptIrp = NULL;
        break;

    case IrpNotCompleted:
        COMPLETE_IRP(ptIrp, IO_NO_INCREMENT);
        break;

    case IrpNotWmi:
    case IrpForward:
    default:
        eStatus                     = STATUS_INVALID_DEVICE_REQUEST;
        ptIrp->IoStatus.Status      = eStatus;
        ptIrp->IoStatus.Information = 0;
        COMPLETE_IRP(ptIrp, IO_NO_INCREMENT);
        break;
    }

    // Keep last status

lblCleanup:
    return eStatus;
}

_Use_decl_annotations_
STATIC
NTSTATUS
sebastian_SmbiosWmiDispatch(
    PDEVICE_OBJECT  ptDeviceObject,
    PIRP            ptIrp
)
{
    NTSTATUS           eStatus            = STATUS_UNSUCCESSFUL;
    PIO_STACK_LOCATION ptStackLocation    = NULL;
    PIRP               ptNewIrp           = NULL;
    PIO_STACK_LOCATION ptNewStackLocation = NULL;
    KEVENT             tEvent             = {0};
    PWNODE_HEADER      ptNodeHeader       = NULL;
    PWNODE_ALL_DATA    ptAllData          = NULL;

    NT_ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    ptStackLocation = IoGetCurrentIrpStackLocation(ptIrp);
    NT_ASSERT(NULL != ptStackLocation);
    NT_ASSERT(IRP_MJ_SYSTEM_CONTROL == ptStackLocation->MajorFunction);

    // Bail if we're not interested in this query.
    if (IRP_MN_QUERY_ALL_DATA != ptStackLocation->MinorFunction ||
        !IsEqualGUID(&g_tMSSMBios_RawSMBiosTables, ptStackLocation->Parameters.WMI.DataPath))
    {
        eStatus = g_pfnOriginalSmbiosWmiDispatch(ptDeviceObject, ptIrp);
        ptIrp   = NULL;
        goto lblCleanup;
    }

    NT_ASSERT(PASSIVE_LEVEL == KeGetCurrentIrql());

    // First time around. Allocate a new IRP and send it again.

    ptNewIrp = IoAllocateIrp(ptDeviceObject->StackSize, FALSE);
    if (NULL == ptNewIrp)
    {
        eStatus = STATUS_INSUFFICIENT_RESOURCES;
        goto lblCleanup;
    }

    ptNewStackLocation = IoGetNextIrpStackLocation(ptNewIrp);
    NT_ASSERT(NULL != ptNewStackLocation);

    // Note: this snippet copied from IoCopyCurrentIrpStackLocationToNext
    RtlMoveMemory(ptNewStackLocation,
                  ptStackLocation,
                  FIELD_OFFSET(IO_STACK_LOCATION, CompletionRoutine));
    ptNewStackLocation->Control = 0;

    KeInitializeEvent(&tEvent, NotificationEvent, FALSE);
    IoSetCompletionRoutine(ptNewIrp,
                           &sebastian_SmbiosWmiCompletion,
                           &tEvent,
                           TRUE,
                           TRUE,
                           TRUE);
    eStatus = sebastian_IoCallDriver(g_pfnOriginalSmbiosWmiDispatch, ptDeviceObject, ptNewIrp);
    if (STATUS_PENDING == eStatus)
    {
        eStatus = KeWaitForSingleObject(&tEvent,
                                        Executive,
                                        KernelMode,
                                        FALSE,
                                        NULL);
        NT_ASSERT(NT_SUCCESS(eStatus));
        eStatus = ptNewIrp->IoStatus.Status;
    }

    // Copy status back to the original IRP
    ptIrp->IoStatus = ptNewIrp->IoStatus;

    if (!NT_SUCCESS(eStatus))
    {
        // The request failed, so there's nothing for us to do.
        goto lblCleanup;
    }

    ptNodeHeader = ptStackLocation->Parameters.WMI.Buffer;
    if (!FlagOn(ptNodeHeader->Flags, WNODE_FLAG_ALL_DATA))
    {
        // Nothing of interest returned.
        goto lblCleanup;
    }

    ptAllData = ptStackLocation->Parameters.WMI.Buffer;
    (VOID) sebastian_PatchSmbiosWmiResponse(ptAllData);

    // Return the correct status
    eStatus = ptIrp->IoStatus.Status;

lblCleanup:
    CLOSE(ptNewIrp, IoFreeIrp);
    COMPLETE_IRP(ptIrp, IO_NO_INCREMENT);

    return eStatus;
}

_Use_decl_annotations_
STATIC
NTSTATUS
sebastian_SmbiosWmiCompletion(
    PDEVICE_OBJECT  ptDeviceObject,
    PIRP            ptIrp,
    PVOID           pvContext
)
{
    PKEVENT ptEvent = (PKEVENT)pvContext;

    NT_ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    UNREFERENCED_PARAMETER(ptDeviceObject);
    NT_ASSERT(NULL != ptIrp);
    NT_ASSERT(NULL != pvContext);

    if (ptIrp->PendingReturned)
    {
        KeSetEvent(ptEvent, EVENT_INCREMENT, FALSE);
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
}

_Use_decl_annotations_
STATIC
NTSTATUS
sebastian_IoCallDriver(
    PDRIVER_DISPATCH    pfnDispatchRoutine,
    PDEVICE_OBJECT      ptDeviceObject,
    PIRP                ptIrp
)
{
    PIO_STACK_LOCATION ptStackLocation = NULL;

    NT_ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    NT_ASSERT(NULL != pfnDispatchRoutine);
    NT_ASSERT(NULL != ptDeviceObject);
    NT_ASSERT(NULL != ptIrp);

    // Nope.
    NT_ASSERT(IRP_MJ_POWER != IoGetNextIrpStackLocation(ptIrp)->MajorFunction);

    // Note: the following was adapted from the WRK. The basic algorithm is unchanged since then.

    --ptIrp->CurrentLocation;

    if (ptIrp->CurrentLocation <= 0)
    {
        KeBugCheckEx(NO_MORE_IRP_STACK_LOCATIONS,
                     (ULONG_PTR)ptIrp,
                     0,
                     0,
                     0);
    }

    ptStackLocation                          = IoGetNextIrpStackLocation(ptIrp);
    ptIrp->Tail.Overlay.CurrentStackLocation = ptStackLocation;

    ptStackLocation->DeviceObject = ptDeviceObject;

    return pfnDispatchRoutine(ptDeviceObject, ptIrp);
}

_Use_decl_annotations_
STATIC
NTSTATUS
sebastian_PatchSmbiosWmiResponse(
    PWNODE_ALL_DATA ptAllData
)
{
    NTSTATUS             eStatus         = STATUS_UNSUCCESSFUL;
    PSMBIOS_WMI_INSTANCE ptInstance      = NULL;
    ULONG                cbInstance      = 0;
    PSMBIOS_TABLE_ENTRY  ptCurrentEntry  = NULL;
    PSYSTEM_INFO_ENTRY   ptSysInfoEntry  = NULL;
    PSTR                 pszManufacturer = NULL;
    PSTR                 pszProductName  = NULL;

    NT_ASSERT(NULL != ptAllData);
    NT_ASSERT(FlagOn(ptAllData->WnodeHeader.Flags, WNODE_FLAG_ALL_DATA));

    if (1 != ptAllData->InstanceCount)
    {
        // There should only be one instance.
        eStatus = STATUS_UNKNOWN_REVISION;
        goto lblCleanup;
    }

    if (!FlagOn(ptAllData->WnodeHeader.Flags, WNODE_FLAG_STATIC_INSTANCE_NAMES))
    {
        // The names should be static.
        eStatus = STATUS_UNKNOWN_REVISION;
        goto lblCleanup;
    }

    if (FlagOn(ptAllData->WnodeHeader.Flags, WNODE_FLAG_FIXED_INSTANCE_SIZE))
    {
        ptInstance =
            (PSMBIOS_WMI_INSTANCE)RtlOffsetToPointer(ptAllData, ptAllData->DataBlockOffset);
        cbInstance = ptAllData->FixedInstanceSize;
    }
    else
    {
        ptInstance = (PSMBIOS_WMI_INSTANCE)RtlOffsetToPointer(
            ptAllData,
            ptAllData->OffsetInstanceDataAndLength[0].OffsetInstanceData
        );
        cbInstance = ptAllData->OffsetInstanceDataAndLength[0].LengthInstanceData;
    }

    if (UFIELD_OFFSET(SMBIOS_WMI_INSTANCE, acSMBIOSTableData[ptInstance->cbLength]) > cbInstance)
    {
        eStatus = STATUS_BUFFER_TOO_SMALL;
        goto lblCleanup;
    }

    for (ptCurrentEntry = (PSMBIOS_TABLE_ENTRY)&ptInstance->acSMBIOSTableData[0];
         (PVOID)ptCurrentEntry < (PVOID)RtlOffsetToPointer(ptInstance, cbInstance);
         ptCurrentEntry = sebastian_NextTableEntry(ptCurrentEntry))
    {
        if (1 == ptCurrentEntry->eType)
        {
            ptSysInfoEntry = (PSYSTEM_INFO_ENTRY)ptCurrentEntry;
            break;
        }
    }
    if (NULL == ptSysInfoEntry)
    {
        eStatus = STATUS_NOT_FOUND;
        goto lblCleanup;
    }

    // Sanity.
    if (ptSysInfoEntry->cbLength < RTL_SIZEOF_THROUGH_FIELD(SYSTEM_INFO_ENTRY, nProductName))
    {
        eStatus = STATUS_BUFFER_TOO_SMALL;
        goto lblCleanup;
    }

    if (0 != ptSysInfoEntry->nManufacturer)
    {
        pszManufacturer =
            sebastian_GetString((PSMBIOS_TABLE_ENTRY)ptSysInfoEntry, ptSysInfoEntry->nManufacturer);
        RtlFillMemory(pszManufacturer, strlen(pszManufacturer) * sizeof(*pszManufacturer), '_');
    }

    if (0 != ptSysInfoEntry->nProductName)
    {
        pszProductName =
            sebastian_GetString((PSMBIOS_TABLE_ENTRY)ptSysInfoEntry, ptSysInfoEntry->nProductName);
        RtlFillMemory(pszProductName, strlen(pszProductName) * sizeof(*pszProductName), '_');
    }

    eStatus = STATUS_SUCCESS;

lblCleanup:
    return eStatus;
}

_Use_decl_annotations_
STATIC
PSMBIOS_TABLE_ENTRY
sebastian_NextTableEntry(
    PSMBIOS_TABLE_ENTRY ptCurrentEntry
)
{
    PSTR pszCurrentString = NULL;

    NT_ASSERT(NULL != ptCurrentEntry);

    pszCurrentString = RtlOffsetToPointer(ptCurrentEntry, ptCurrentEntry->cbLength);

    // Note: for simplicity, the code assumes that the SMBIOS table is well-formed.
    // Since we're getting it from another driver, it's a valid assumption.

    // See the SMBIOS spec.
    // (https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.3.0.pdf)
    // Sections 6.1.2 and 6.1.3.
    do
    {
        pszCurrentString += strlen(pszCurrentString) + 1;
    } while ('\0' != *pszCurrentString);
    pszCurrentString += 1;

    return (PSMBIOS_TABLE_ENTRY)pszCurrentString;
}

_Use_decl_annotations_
STATIC
PSTR
sebastian_GetString(
    PSMBIOS_TABLE_ENTRY ptTableEntry,
    UCHAR               nString
)
{
    UCHAR nIndex           = 0;
    PSTR  pszCurrentString = NULL;

    NT_ASSERT(NULL != ptTableEntry);
    NT_ASSERT(0 != nString);

    pszCurrentString = RtlOffsetToPointer(ptTableEntry, ptTableEntry->cbLength);

    for (nIndex = 1; nIndex < nString; ++nIndex)
    {
        pszCurrentString += strlen(pszCurrentString) + 1;
    }

    return pszCurrentString;
}

#pragma alloc_text("PAGE", sebastian_GetDisplayWmiBaseName)
_Use_decl_annotations_
STATIC
NTSTATUS
sebastian_GetDisplayWmiBaseName(
    PUNICODE_STRING pusRegistryPath,
    PUNICODE_STRING pusBaseName
)
{
    NTSTATUS            eStatus     = STATUS_UNSUCCESSFUL;
    OBJECT_ATTRIBUTES   tAttributes = RTL_INIT_OBJECT_ATTRIBUTES(pusRegistryPath,
                                                                 OBJ_KERNEL_HANDLE
                                                                    | OBJ_CASE_INSENSITIVE);
    HANDLE              hDriverKey  = NULL;
    UNICODE_STRING      usBaseName  = {0};

    RTL_QUERY_REGISTRY_TABLE atTable[] = {
        {
            NULL,
            RTL_QUERY_REGISTRY_SUBKEY,
            L"Parameters",
            NULL,
            REG_NONE,
            NULL,
            0
        },
        {
            NULL,
            RTL_QUERY_REGISTRY_REQUIRED
                | RTL_QUERY_REGISTRY_NOEXPAND
                | RTL_QUERY_REGISTRY_DIRECT
                | RTL_QUERY_REGISTRY_TYPECHECK,
            L"DisplayWmiBaseName",
            &usBaseName,
            (REG_SZ << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE,
            NULL,
            0
        },
        {0}
    };

    PAGED_CODE();
    NT_ASSERT(PASSIVE_LEVEL == KeGetCurrentIrql());

    NT_ASSERT(NULL != pusRegistryPath);
    NT_ASSERT(NULL != pusRegistryPath->Buffer);
    NT_ASSERT(NULL != pusBaseName);

    eStatus = ZwCreateKey(&hDriverKey,
                          KEY_QUERY_VALUE,
                          &tAttributes,
                          0,
                          NULL,
                          REG_OPTION_NON_VOLATILE,
                          NULL);
    if (!NT_SUCCESS(eStatus))
    {
        goto lblCleanup;
    }

    eStatus = RtlQueryRegistryValues(RTL_REGISTRY_HANDLE,
                                     hDriverKey,
                                     atTable, 
                                     NULL, 
                                     NULL);
    if (!NT_SUCCESS(eStatus))
    {
        goto lblCleanup;
    }

    // Transfer ownership:
    *pusBaseName = usBaseName;
    usBaseName.Buffer = NULL;

    eStatus = STATUS_SUCCESS;

lblCleanup:
    CLOSE(usBaseName.Buffer, ExFreePool);
    CLOSE(hDriverKey, ZwClose);

    return eStatus;
}

#pragma alloc_text("PAGE", sebastian_WmiQueryRegInfo)
_Use_decl_annotations_
STATIC
NTSTATUS
sebastian_WmiQueryRegInfo(
    PDEVICE_OBJECT      ptDeviceObject,
    PULONG              pfRegFlags,
    PUNICODE_STRING     pusInstanceName,
    PUNICODE_STRING *   ppusRegistryPath,
    PUNICODE_STRING     pusMofResourceName,
    PDEVICE_OBJECT *    pptPdo
)
{
    NTSTATUS       eStatus                = STATUS_UNSUCCESSFUL;
    UNICODE_STRING usInstanceBaseNameCopy = {0};

    PAGED_CODE();
    NT_ASSERT(PASSIVE_LEVEL == KeGetCurrentIrql());

    UNREFERENCED_PARAMETER(ptDeviceObject);
    NT_ASSERT(NULL != pfRegFlags);
    NT_ASSERT(NULL != pusInstanceName);
    NT_ASSERT(NULL != ppusRegistryPath);
    UNREFERENCED_PARAMETER(pusMofResourceName);
    UNREFERENCED_PARAMETER(pptPdo);

    *pfRegFlags = WMIREG_FLAG_INSTANCE_BASENAME;

    NT_ASSERT(NULL != g_usRegistryPath.Buffer);
    *ppusRegistryPath = &g_usRegistryPath;

    usInstanceBaseNameCopy        = g_usDisplayWmiBaseName;
    usInstanceBaseNameCopy.Buffer = ExAllocatePoolWithTag(
        NonPagedPoolNx,
        g_usDisplayWmiBaseName.MaximumLength,
        SEBASTIAN_POOL_TAG);
    if (NULL == usInstanceBaseNameCopy.Buffer)
    {
        eStatus = STATUS_INSUFFICIENT_RESOURCES;
        goto lblCleanup;
    }
    RtlMoveMemory(
        usInstanceBaseNameCopy.Buffer,
        g_usDisplayWmiBaseName.Buffer,
        g_usDisplayWmiBaseName.MaximumLength);

    // Transfer ownership:
    *pusInstanceName              = usInstanceBaseNameCopy;
    usInstanceBaseNameCopy.Buffer = NULL;

    eStatus = STATUS_SUCCESS;

lblCleanup:
    CLOSE(usInstanceBaseNameCopy.Buffer, ExFreePool);

    return eStatus;
}

#pragma alloc_text("PAGE", sebastian_WmiQueryDataBlock)
_Use_decl_annotations_
STATIC
NTSTATUS
sebastian_WmiQueryDataBlock(
    PDEVICE_OBJECT  ptDeviceObject,
    PIRP            ptIrp,
    ULONG           nGuidIndex,
    ULONG           nInstanceIndex,
    ULONG           nInstanceCount,
    PULONG          pnInstanceLengthArray,
    ULONG           cbBufferAvailable,
    PUCHAR          pcBuffer
)
{
    NTSTATUS                     eStatus  = STATUS_UNSUCCESSFUL;
    ULONG                        cbNeeded = 0;
    WmiMonitorBasicDisplayParams tParams  = {0};

    PAGED_CODE();
    NT_ASSERT(PASSIVE_LEVEL == KeGetCurrentIrql());

    NT_ASSERT(NULL != ptDeviceObject);
    NT_ASSERT(NULL != ptIrp);

    if (nGuidIndex > 0)
    {
        eStatus = STATUS_WMI_GUID_NOT_FOUND;
        goto lblCleanup;
    }

    if (nInstanceIndex != 0 || nInstanceCount > 1)
    {
        eStatus = STATUS_WMI_INSTANCE_NOT_FOUND;
        goto lblCleanup;
    }

    if (0 == nInstanceCount)
    {
        cbNeeded = 0;
        eStatus  = STATUS_SUCCESS;
        goto lblCleanup;
    }

    cbNeeded = sizeof(tParams);
    if (cbBufferAvailable < cbNeeded)
    {
        eStatus = STATUS_BUFFER_TOO_SMALL;
        goto lblCleanup;
    }

    NT_ASSERT(NULL != pnInstanceLengthArray);
    NT_ASSERT(NULL != pcBuffer);

    tParams.VideoInputType                                  = 1; // Digital
    tParams.MaxHorizontalImageSize                          = 0;
    tParams.MaxVerticalImageSize                            = 0;
    tParams.DisplayTransferCharacteristic                   = 120;
    tParams.SupportedDisplayFeatures.StandbySupported       = FALSE;
    tParams.SupportedDisplayFeatures.SuspendSupported       = FALSE;
    tParams.SupportedDisplayFeatures.ActiveOffSupported     = FALSE;
    tParams.SupportedDisplayFeatures.DisplayType            = 1; // RGB
    tParams.SupportedDisplayFeatures.sRGBSupported          = FALSE;
    tParams.SupportedDisplayFeatures.HasPreferredTimingMode = FALSE;
    tParams.SupportedDisplayFeatures.GTFSupported           = FALSE;

    *(PWmiMonitorBasicDisplayParams)pcBuffer = tParams;
    *pnInstanceLengthArray                   = cbNeeded;

    eStatus = STATUS_SUCCESS;

lblCleanup:
    return WmiCompleteRequest(ptDeviceObject, ptIrp, eStatus, cbNeeded, IO_NO_INCREMENT);
}
