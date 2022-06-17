#pragma once

#include <ntifs.h>


/**
 * Initializes the module.
 *
 * @param pusRegistryPath The registry path of the driver.
 */
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
SEBASTIAN_Initialize(
    _In_    PUNICODE_STRING pusRegistryPath
);

/**
 * Uninitializes the module.
 *
 * @remark This function must not be called after hook installation.
 */
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
VOID
SEBASTIAN_Uninitialize(VOID);

/**
 * Installs the hooks.
 *
 * @param ptWmiDevice Device to be used for WMI queries.
 */
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
SEBASTIAN_Hook(
    _In_    PDEVICE_OBJECT  ptWmiDevice
);

/**
 * WMI IRP dispatch routine.
 */
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
SEBASTIAN_WmiDispatch(
    _In_    PDEVICE_OBJECT  ptDeviceObject,
    _In_    PIRP            ptIrp
);
