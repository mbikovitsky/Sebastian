#include <cassert>
#include <cstdlib>
#include <cstring>

#include <Windows.h>

#include <SetupAPI.h>
#include <WbemIdl.h>
#include <objbase.h>

STATIC
HRESULT
coinstaller_GetDisplayWmiBaseName(
    _Out_   PWSTR * ppwszInstanceName
)
{
    HRESULT                hrResult        = E_FAIL;
    BOOL                   bUninitCom      = FALSE;
    IWbemLocator *         piLocator       = nullptr;
    BSTR                   bstrNamespace   = nullptr;
    IWbemServices *        piServices      = nullptr;
    BSTR                   bstrLanguage    = nullptr;
    BSTR                   bstrQuery       = nullptr;
    IEnumWbemClassObject * piEnumerator    = nullptr;
    IWbemClassObject *     apiObjects[2]   = {nullptr};
    ULONG                  nReturned       = 0;
    VARIANT                vInstanceName   = {0};
    BOOL                   bClearVariant   = FALSE;
    UINT                   cchInstanceName = 0;

    assert(nullptr != ppwszInstanceName);

    hrResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hrResult))
    {
        goto lblCleanup;
    }
    bUninitCom = TRUE;

    hrResult = CoCreateInstance(CLSID_WbemLocator,
                                nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_IWbemLocator,
                                reinterpret_cast<PVOID *>(&piLocator));
    if (FAILED(hrResult))
    {
        goto lblCleanup;
    }

    bstrNamespace = SysAllocString(L"root\\wmi");
    if (nullptr == bstrNamespace)
    {
        hrResult = E_OUTOFMEMORY;
        goto lblCleanup;
    }

    hrResult = piLocator->ConnectServer(bstrNamespace,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        0,
                                        nullptr,
                                        nullptr,
                                        &piServices);
    if (FAILED(hrResult))
    {
        goto lblCleanup;
    }

    hrResult = CoSetProxyBlanket(piServices,
                                 RPC_C_AUTHN_DEFAULT,
                                 RPC_C_AUTHZ_DEFAULT,
                                 nullptr,
                                 RPC_C_AUTHN_LEVEL_DEFAULT,
                                 RPC_C_IMP_LEVEL_IMPERSONATE,
                                 nullptr,
                                 EOAC_NONE);
    if (FAILED(hrResult))
    {
        goto lblCleanup;
    }

    bstrLanguage = SysAllocString(L"WQL");
    if (nullptr == bstrLanguage)
    {
        hrResult = E_OUTOFMEMORY;
        goto lblCleanup;
    }

    bstrQuery = SysAllocString(L"SELECT * FROM WmiMonitorConnectionParams");
    if (nullptr == bstrQuery)
    {
        hrResult = E_OUTOFMEMORY;
        goto lblCleanup;
    }

    hrResult = piServices->ExecQuery(bstrLanguage,
                                     bstrQuery,
                                     WBEM_FLAG_FORWARD_ONLY,
                                     nullptr,
                                     &piEnumerator);
    if (FAILED(hrResult))
    {
        goto lblCleanup;
    }

    hrResult = piEnumerator->Next(WBEM_INFINITE,
                                  2,
                                  apiObjects,
                                  &nReturned);
    if (FAILED(hrResult))
    {
        goto lblCleanup;
    }

    if (nReturned > 1)
    {
        hrResult = HRESULT_FROM_WIN32(ERROR_TOO_MANY_DESCRIPTORS);
        goto lblCleanup;
    }
    if (nReturned < 1)
    {
        hrResult = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        goto lblCleanup;
    }

    hrResult = apiObjects[0]->Get(L"InstanceName", 0, &vInstanceName, nullptr, nullptr);
    if (FAILED(hrResult))
    {
        goto lblCleanup;
    }
    bClearVariant = TRUE;

    if (VT_BSTR != vInstanceName.vt)
    {
        hrResult = E_UNEXPECTED;
        goto lblCleanup;
    }

    // Check that the instance name ends in "_0", and remove the 0.
    // The kernel WMI component will then add the 0 back when it generates
    // an instance name from this basename.

    cchInstanceName = SysStringLen(vInstanceName.bstrVal);

    if (cchInstanceName < 2)
    {
        hrResult = HRESULT_FROM_WIN32(ERROR_PWD_TOO_SHORT);
        goto lblCleanup;
    }

    if (L'0' != vInstanceName.bstrVal[cchInstanceName - 1])
    {
        hrResult = E_UNEXPECTED;
        goto lblCleanup;
    }

    if (L'_' != vInstanceName.bstrVal[cchInstanceName - 2])
    {
        hrResult = E_UNEXPECTED;
        goto lblCleanup;
    }

    *ppwszInstanceName = (PWSTR)std::calloc(cchInstanceName, sizeof(WCHAR));
    if (nullptr == *ppwszInstanceName)
    {
        hrResult = E_OUTOFMEMORY;
        goto lblCleanup;
    }
    RtlMoveMemory(*ppwszInstanceName, vInstanceName.bstrVal, (cchInstanceName - 1) * sizeof(WCHAR));

    hrResult = S_OK;

lblCleanup:
    if (bClearVariant)
    {
        VariantClear(&vInstanceName);
        bClearVariant = FALSE;
    }
    if (nullptr != apiObjects[1])
    {
        apiObjects[1]->Release();
        apiObjects[1] = nullptr;
    }
    if (nullptr != apiObjects[0])
    {
        apiObjects[0]->Release();
        apiObjects[0] = nullptr;
    }
    if (nullptr != piEnumerator)
    {
        piEnumerator->Release();
        piEnumerator = nullptr;
    }
    if (nullptr != bstrQuery)
    {
        SysFreeString(bstrQuery);
        bstrQuery = nullptr;
    }
    if (nullptr != bstrLanguage)
    {
        SysFreeString(bstrLanguage);
        bstrLanguage = nullptr;
    }
    if (nullptr != piServices)
    {
        piServices->Release();
        piServices = nullptr;
    }
    if (nullptr != bstrNamespace)
    {
        SysFreeString(bstrNamespace);
        bstrNamespace = nullptr;
    }
    if (nullptr != piLocator)
    {
        piLocator->Release();
        piLocator = nullptr;
    }
    if (bUninitCom)
    {
        CoUninitialize();
        bUninitCom = FALSE;
    }

    return hrResult;
}

EXTERN_C
DWORD
CALLBACK
CoDeviceInstall(
    _In_        DI_FUNCTION                 eInstallFunction,
    _In_        HDEVINFO                    hDeviceInfoSet,
    _In_opt_    PSP_DEVINFO_DATA            ptDeviceInfoData,
    _In_opt_    PCOINSTALLER_CONTEXT_DATA   ptContext
)
{
    DWORD eResult      = ERROR_ARENA_TRASHED;
    PWSTR pwszBaseName = nullptr;
    HKEY  hRegKey      = nullptr;

    if (DIF_INSTALLDEVICE != eInstallFunction)
    {
        eResult = NO_ERROR;
        goto lblCleanup;
    }

    if (nullptr != ptContext && ptContext->PostProcessing)
    {
        eResult = NO_ERROR;
        goto lblCleanup;
    }

    UNREFERENCED_PARAMETER(hDeviceInfoSet);
    UNREFERENCED_PARAMETER(ptDeviceInfoData);

    if (FAILED(coinstaller_GetDisplayWmiBaseName(&pwszBaseName)))
    {
        eResult = ERROR_NOT_FOUND;
        goto lblCleanup;
    }

    eResult = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                              L"SYSTEM\\CurrentControlSet\\Services\\Sebastian\\Parameters",
                              0,
                              nullptr,
                              REG_OPTION_NON_VOLATILE,
                              KEY_SET_VALUE,
                              nullptr,
                              &hRegKey,
                              nullptr);
    if (ERROR_SUCCESS != eResult)
    {
        goto lblCleanup;
    }

    eResult = RegSetValueExW(hRegKey,
                             L"DisplayWmiBaseName",
                             0,
                             REG_SZ,
                             (BYTE CONST *)pwszBaseName,
                             (wcslen(pwszBaseName) + 1) * sizeof(WCHAR));
    if (ERROR_SUCCESS != eResult)
    {
        goto lblCleanup;
    }

    eResult = NO_ERROR;

lblCleanup:
    if (nullptr != hRegKey)
    {
        RegCloseKey(hRegKey);
        hRegKey = nullptr;
    }
    if (nullptr != pwszBaseName)
    {
        std::free(pwszBaseName);
        pwszBaseName = nullptr;
    }

    return eResult;
}
