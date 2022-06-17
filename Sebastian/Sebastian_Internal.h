#pragma once

#include <ntifs.h>
#include <wmistr.h>


/**
 * Pool tag for allocations made by this module.
 */
#define SEBASTIAN_POOL_TAG ('tsbS')


/**
 * Defines the data returned by the SMBIOS WMI provider.
 *
 * @see https://download.microsoft.com/download/5/D/6/5D6EAF2B-7DDF-476B-93DC-7CF0072878E6/SMBIOS.doc
 */
typedef struct tagSMBIOS_WMI_INSTANCE
{
    UCHAR bUsed20CallingMethod;
    UCHAR nSMBIOSMajorVersion;
    UCHAR nSMBIOSMinorVersion;
    UCHAR nDmiRevision;
    ULONG cbLength;
    UCHAR acSMBIOSTableData[ANYSIZE_ARRAY];
} SMBIOS_WMI_INSTANCE, *PSMBIOS_WMI_INSTANCE;
typedef SMBIOS_WMI_INSTANCE CONST * PCSMBIOS_WMI_INSTANCE;

/**
 * Structure of a single SMBIOS table entry.
 */
typedef struct tagSMBIOS_TABLE_ENTRY
{
    UCHAR  eType;
    UCHAR  cbLength;
    USHORT eHandle;
    UCHAR  acData[ANYSIZE_ARRAY];
} SMBIOS_TABLE_ENTRY, *PSMBIOS_TABLE_ENTRY;
typedef SMBIOS_TABLE_ENTRY CONST * PCSMBIOS_TABLE_ENTRY;

/**
 * Partial structure of the SMBIOS System Information table.
 */
typedef struct tagSYSTEM_INFO_ENTRY
{
    UCHAR  eType;
    UCHAR  cbLength;
    USHORT eHandle;
    UCHAR  nManufacturer;
    UCHAR  nProductName;
    UCHAR  acReserved[ANYSIZE_ARRAY];
} SYSTEM_INFO_ENTRY, *PSYSTEM_INFO_ENTRY;
typedef SYSTEM_INFO_ENTRY CONST * PCSYSTEM_INFO_ENTRY;

/**
 * @see https://docs.microsoft.com/en-us/windows/win32/wmicoreprov/wmimonitorbasicdisplayparams
 */
typedef struct tagWmiMonitorSupportedDisplayFeatures
{
    // VESA DPMS Standby support
    BOOLEAN StandbySupported;

    // VESA DPMS Suspend support
    BOOLEAN SuspendSupported;

    // Active Off/Very Low Power Support
    BOOLEAN ActiveOffSupported;

    // Display type
    UCHAR DisplayType;

    // sRGB support
    BOOLEAN sRGBSupported;

    // Has a preferred timing mode
    BOOLEAN HasPreferredTimingMode;

    // GTF support
    BOOLEAN GTFSupported;
} WmiMonitorSupportedDisplayFeatures, *PWmiMonitorSupportedDisplayFeatures;

/**
 * @see https://docs.microsoft.com/en-us/windows/win32/wmicoreprov/wmimonitorbasicdisplayparams
 */
typedef struct tagWmiMonitorBasicDisplayParams
{
    // Video input type
    UCHAR VideoInputType;

    // Max horizontal image size (in cm)
    UCHAR MaxHorizontalImageSize;

    // Max vertical image size (in cm)
    UCHAR MaxVerticalImageSize;

    // Display transfer characteristic (100*Gamma-100)
    UCHAR DisplayTransferCharacteristic;

    // Supported display features
    WmiMonitorSupportedDisplayFeatures SupportedDisplayFeatures;
} WmiMonitorBasicDisplayParams, *PWmiMonitorBasicDisplayParams;


/**
 * Hook for the SMBIOS driver's WMI dispatch routine.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
STATIC
NTSTATUS
sebastian_SmbiosWmiDispatch(
    _In_    PDEVICE_OBJECT  ptDeviceObject,
    _In_    PIRP            ptIrp
);

/**
 * Completion routine for SMBIOS WMI IRPs.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
STATIC
NTSTATUS
sebastian_SmbiosWmiCompletion(
    _In_    PDEVICE_OBJECT  ptDeviceObject,
    _In_    PIRP            ptIrp,
    _In_    PVOID           pvContext
);

/**
 * Custom implementation of IoCallDriver, that allows
 * specifying a different dispatch routine.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
STATIC
NTSTATUS
sebastian_IoCallDriver(
    _In_    PDRIVER_DISPATCH    pfnDispatchRoutine,
    _In_    PDEVICE_OBJECT      ptDeviceObject,
    _In_    PIRP                ptIrp);

/**
 * Patches the SMBIOS WMI query result to be less suspicious.
 */
STATIC
NTSTATUS
sebastian_PatchSmbiosWmiResponse(
    _In_    PWNODE_ALL_DATA ptAllData
);

/**
 * Given a pointer to an SMBIOS table entry, returns
 * a pointer to the next entry.
 */
STATIC
PSMBIOS_TABLE_ENTRY
sebastian_NextTableEntry(
    _In_    PSMBIOS_TABLE_ENTRY ptCurrentEntry
);

/**
 * Obtains a string at the given 1-based index
 * in the given SMBIOS table.
 */
STATIC
PSTR
sebastian_GetString(
    _In_    PSMBIOS_TABLE_ENTRY ptTableEntry,
    _In_    UCHAR               nString
);

/**
 * Obtains the base name to be used for monitor
 * WMI queries.
 */
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
STATIC
NTSTATUS
sebastian_GetDisplayWmiBaseName(
    _In_    PUNICODE_STRING pusRegistryPath,
    _Out_   PUNICODE_STRING pusBaseName
);

/**
 * WMI registration callback for monitor WMI queries.
 */
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
STATIC
NTSTATUS
sebastian_WmiQueryRegInfo(
    _In_        PDEVICE_OBJECT      ptDeviceObject,
    _Out_       PULONG              pfRegFlags,
    _Out_       PUNICODE_STRING     pusInstanceName,
    _Outptr_    PUNICODE_STRING *   ppusRegistryPath,
    _Out_       PUNICODE_STRING     pusMofResourceName,
    _Outptr_    PDEVICE_OBJECT *    pptPdo
);

/**
 * WMI query callback for monitor WMI queries.
 */
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
STATIC
NTSTATUS
sebastian_WmiQueryDataBlock(
    _In_        PDEVICE_OBJECT  ptDeviceObject,
    _In_        PIRP            ptIrp,
    _In_        ULONG           nGuidIndex,
    _In_        ULONG           nInstanceIndex,
    _In_        ULONG           nInstanceCount,
    _Inout_opt_ PULONG          pnInstanceLengthArray,
    _In_        ULONG           cbBufferAvailable,
    _Out_opt_   PUCHAR          pcBuffer
);
