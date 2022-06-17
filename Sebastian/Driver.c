#include <ntifs.h>

#include "Common.h"
#include "Sebastian.h"


DRIVER_INITIALIZE DriverEntry;


/**
 * Dispatch routine for WMI IRPs.
 */
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
STATIC
NTSTATUS
driver_DispatchSystemControl(
    _In_    PDEVICE_OBJECT  ptDeviceObject,
    _In_    PIRP            ptIrp
)
{
    NT_ASSERT(PASSIVE_LEVEL == KeGetCurrentIrql());

    NT_ASSERT(NULL != ptDeviceObject);
    NT_ASSERT(NULL != ptIrp);

    return SEBASTIAN_WmiDispatch(ptDeviceObject, ptIrp);
}

/**
 * Driver entry point.
 */
#pragma alloc_text("INIT", DriverEntry)
_Use_decl_annotations_
NTSTATUS
DriverEntry(
    PDRIVER_OBJECT  ptDriverObject,
    PUNICODE_STRING pusRegistryPath
)
{
    NTSTATUS        eStatus        = STATUS_UNSUCCESSFUL;
    PDEVICE_OBJECT  ptDeviceObject = NULL;
    BOOLEAN         bUninit        = FALSE;

    NT_ASSERT(NULL != ptDriverObject);
    NT_ASSERT(NULL != pusRegistryPath);

    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);

    ptDriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = &driver_DispatchSystemControl;

    eStatus = IoCreateDevice(ptDriverObject,
                             0,
                             NULL,
                             FILE_DEVICE_UNKNOWN,
                             FILE_DEVICE_SECURE_OPEN,
                             FALSE,
                             &ptDeviceObject);
    if (!NT_SUCCESS(eStatus))
    {
        goto lblCleanup;
    }

    SetFlag(ptDeviceObject->Flags, DO_BUFFERED_IO);

    ClearFlag(ptDeviceObject->Flags, DO_DEVICE_INITIALIZING);

    eStatus = SEBASTIAN_Initialize(pusRegistryPath);
    if (!NT_SUCCESS(eStatus))
    {
        goto lblCleanup;
    }
    bUninit = TRUE;

    eStatus = SEBASTIAN_Hook(ptDeviceObject);
    if (!NT_SUCCESS(eStatus))
    {
        goto lblCleanup;
    }
    bUninit = FALSE;

    // NO FAILURE PAST THIS POINT!

    // Transfer ownership:
    ptDeviceObject = NULL;

    eStatus = STATUS_SUCCESS;

lblCleanup:
    if (bUninit)
    {
        SEBASTIAN_Uninitialize();
        bUninit = FALSE;
    }
    CLOSE(ptDeviceObject, IoDeleteDevice);

    return eStatus;
}
