/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <windows.h>
#include <dxgi.h>
#include <stdio.h>
#include <wchar.h>

int wmain(void)
{
    HMODULE d;
    typedef HRESULT (WINAPI *CreateFactoryFn)(REFIID, void**);
    CreateFactoryFn create;
    IDXGIFactory1 *factory = NULL;
    HRESULT hr;
    UINT i;

    setvbuf(stdout, NULL, _IONBF, 0);
    d = LoadLibraryW(L"dxgi.dll");
    printf("dxgi=%p\n", (void*)d);
    if (!d) return 1;
    create = (CreateFactoryFn)(void*)GetProcAddress(d, "CreateDXGIFactory1");
    printf("create=%p\n", (void*)create);
    if (!create) return 1;
    hr = create(&IID_IDXGIFactory1, (void**)&factory);
    printf("hr=0x%08lX factory=%p\n", (unsigned long)hr, (void*)factory);
    if (FAILED(hr) || !factory) return 1;
    for (i = 0; ; i++) {
        IDXGIAdapter1 *adapter = NULL;
        DXGI_ADAPTER_DESC1 desc;
        HRESULT ar = factory->lpVtbl->EnumAdapters1(factory, i, &adapter);
        printf("i=%u ar=0x%08lX adapter=%p\n", i, (unsigned long)ar, (void*)adapter);
        if (ar != S_OK) break;
        if (adapter->lpVtbl->GetDesc1(adapter, &desc) == S_OK) {
            printf("  flags=0x%X vendor=0x%04X vram=%llu desc=%ls\n",
                   desc.Flags, desc.VendorId,
                   (unsigned long long)desc.DedicatedVideoMemory, desc.Description);
        }
        adapter->lpVtbl->Release(adapter);
    }
    factory->lpVtbl->Release(factory);
    FreeLibrary(d);
    printf("done\n");
    return 0;
}
