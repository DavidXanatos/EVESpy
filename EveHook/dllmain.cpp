#include "stdafx.h"
#include <Windows.h>
#include <d3d11.h>
#include <detours.h>
#include <DXGI.h>
#include <DirectXTex.h>
#include <ScreenGrab.h>
#include <wincodec.h>
#include <opencv2/core.hpp>
#include <opencv2/core/directx.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/objdetect.hpp>

#include <Strsafe.h>
#include "resource.h"

using namespace cv;
using namespace DirectX;


typedef HRESULT(WINAPI *D3D11CreateDeviceAndSwapChain_t)(IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, DXGI_SWAP_CHAIN_DESC *pSwapChainDesc, IDXGISwapChain **ppSwapChain, ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppImmediateContext);
typedef void(WINAPI *ClearRenderTargetView_t)(ID3D11DeviceContext *pImmediateContext, ID3D11RenderTargetView *pRenderTargetView, const FLOAT ColorRGBA[4]);
typedef void(WINAPI *D3D11PresentHook) (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);



// only INITIALIZED variables in this block will actually end up in the shared section!!!
//#pragma data_seg (".shared")
D3D11CreateDeviceAndSwapChain_t pD3D11CreateDeviceAndSwapChain = NULL;
ClearRenderTargetView_t pClearRenderTargetView = NULL;
static HHOOK hHook = NULL;
static HWND hwnd = NULL;
BOOL inTarget = FALSE;
static char targetName[MAX_PATH] = "exefile.exe";
// global declarations
IDXGISwapChain *pSwapChain = NULL;             // the pointer to the swap chain interface
ID3D11Device *pDevice = NULL;                     // the pointer to our Direct3D device interface
ID3D11DeviceContext *pDeviceContext;           // the pointer to our Direct3D device contex
ID3D11Texture2D *pSurface = NULL, *pNewTexture = NULL;
ID3D11RenderTargetView *backbuffer = NULL;
D3D11PresentHook phookD3D11Present = NULL;
DWORD_PTR* pSwapChainVtable = NULL;
DWORD_PTR* pDeviceContextVTable = NULL;
DWORD startTime;
DWORD currentTime;
//#pragma data_seg ()
CascadeClassifier stargateClassifier;

/* Hook other window proc
targetWindow = hFocusWindow != NULL ? hFocusWindow : pPresentationParameters->hDeviceWindow;
if (targetWindow != NULL)
{
	RECT rect;
	GetClientRect(targetWindow, &rect);
	TwWindowSize(rect.right - rect.left, rect.bottom - rect.top);
	OldWindowProc = (WNDPROC)SetWindowLongPtr(targetWindow, GWL_WNDPROC, (LONG_PTR)WindowProc);
}
*/


void WINAPI hkClearRenderTargetView(ID3D11DeviceContext *pImmediateContext, ID3D11RenderTargetView *pRenderTargetView, const FLOAT ColorRGBA[4])
{
	static int num = 0;
	ID3D11Resource* pResource = nullptr;
	

	//if (GetAsyncKeyState(VK_NUMPAD1) & 1)
	//	MessageBox(0, 0, 0, 0);
	currentTime = GetTickCount();
	if (currentTime - startTime > 30000)
	{
		OutputDebugString(TEXT("Trying to get pic\n"));
		startTime = currentTime;

		pRenderTargetView->GetResource(&pResource);

		HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pSurface);

		if (SUCCEEDED(hr))
		{
			
			TCHAR msg[256];
			Mat ds;
			HRSRC XmlResource = FindResource(NULL, MAKEINTRESOURCE(IDR_XML1), L"xml");
			HGLOBAL XmlResourceData = LoadResource(NULL, XmlResource);
			PVOID pStargateClassifier = LockResource(XmlResourceData);


			FileStorage fs((String &)pStargateClassifier, FileStorage::READ | FileStorage::MEMORY);
			stargateClassifier.read(fs.getFirstTopLevelNode());

			StringCchPrintf(msg, sizeof(msg), TEXT("screenshot-exefile%d.JPG"), num++);
			OutputDebugString(msg);
			cv::directx::convertFromD3D11Texture2D(pSurface, ds);
			SaveWICTextureToFile( pImmediateContext , pSurface, GUID_ContainerFormatJpeg, msg);
			pSurface->Release();
		}

	}

	

	
	return pClearRenderTargetView(pImmediateContext, pRenderTargetView, ColorRGBA);
}


HRESULT WINAPI hkD3D11CreateDeviceAndSwapChain(IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, DXGI_SWAP_CHAIN_DESC *pSwapChainDesc, IDXGISwapChain **ppSwapChain, ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppImmediateContext)
{
	HRESULT hResult = pD3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);

	OutputDebugString(TEXT("Trying to get pointer!...\n"));

	if (SUCCEEDED(hResult))
	{
		pDevice = *ppDevice;
		pSwapChain = *ppSwapChain;
		pDeviceContext = *ppImmediateContext;

		
		OutputDebugString(TEXT("Got pointer!...\n"));

		pSwapChainVtable = (DWORD_PTR*)pSwapChain;
		pSwapChainVtable = (DWORD_PTR*)pSwapChainVtable[0];

		pDeviceContextVTable = (DWORD_PTR*)pDeviceContext;
		pDeviceContextVTable = (DWORD_PTR*)pDeviceContextVTable[0];


		pClearRenderTargetView = (ClearRenderTargetView_t)(PVOID*)(BYTE*)pDeviceContextVTable[50];

		DetourTransactionBegin();

		LONG errorCode = DetourAttach((PVOID*)&pClearRenderTargetView, hkClearRenderTargetView);

		DetourTransactionCommit();

		if (!errorCode)
		{
			OutputDebugString(TEXT("[2] Detour successful\n"));
		}
		else
		{
			TCHAR msg[256];
			wsprintf(msg, TEXT("Cannot install detour, code: %d\n"), GetLastError());
			OutputDebugString(msg);
		}
	}

	return hResult;
}



// dllmain.cpp : Defines the entry point for the DLL application.
LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (HCBT_CREATEWND == nCode)
	{
		char szPath[MAX_PATH];

		ZeroMemory(szPath, MAX_PATH);
		if (inTarget == FALSE)
		{
			if (GetModuleFileNameA(NULL, szPath, MAX_PATH))
			{
				OutputDebugStringA(szPath);

				if (strstr(szPath, targetName))
				{
					HMODULE dwD3D11 = NULL;

					OutputDebugString(TEXT("Found target process.  Hooking DirectX...\n"));
				
					// Loop until we get the Direct 3D DLL
					while (!dwD3D11)
					{
						dwD3D11 = (HMODULE)GetModuleHandle(TEXT("d3d11.dll"));
						Sleep(250);
					}					

					pD3D11CreateDeviceAndSwapChain  = (D3D11CreateDeviceAndSwapChain_t)GetProcAddress(dwD3D11, "D3D11CreateDeviceAndSwapChain");
					

					DetourTransactionBegin();
					// DetourUpdateThread(GetCurrentThread());
					LONG errorCode = DetourAttach((PVOID*)&pD3D11CreateDeviceAndSwapChain, hkD3D11CreateDeviceAndSwapChain);
					if (!errorCode)
					{
						OutputDebugString(TEXT("[1] Detour successful\n"));
					}
					else
					{
						TCHAR msg[256];
						wsprintf(msg, TEXT("Cannot install detour, code: %d\n"), GetLastError());
						OutputDebugString(msg);
					}
					
					//SendMessage(hwnd, 0xBEEF, GetCurrentProcessId(), 0);
					DetourTransactionCommit();
					inTarget = TRUE;

				}
				else
				{
					OutputDebugString(TEXT("Did not find the target\n"));
				}
			}
		}
	}

	return CallNextHookEx(hHook, nCode, wParam, lParam);
}

__declspec(dllexport) void CALLBACK InstallHook(HWND hWnd, const char *pName, HMODULE hins)
{
	hwnd = hWnd;
	startTime = GetTickCount();
	targetName[0] = 0;
	if (pName)
	{
		strncpy(targetName, pName, sizeof(targetName));
	}

	if (hHook == NULL)
	{
		hHook = SetWindowsHookEx(WH_CBT, (HOOKPROC)HookProc, hins, 0);

		if (NULL == hHook)
		{
			TCHAR msg[256];
			wsprintf(msg, TEXT("Cannot install hook, code: %d\n"), GetLastError());
			MessageBox(hwnd, msg, TEXT("error"), MB_ICONERROR);
		}

	}
}

__declspec(dllexport) void CALLBACK ReleaseHook()
{
	if (hHook != NULL)
	{
		TCHAR msg[256];
		BOOL bRes = UnhookWindowsHookEx(hHook);

		DetourTransactionBegin();
		// DetourUpdateThread(GetCurrentThread());
		DetourDetach((PVOID*)(PVOID*)&pD3D11CreateDeviceAndSwapChain, hkD3D11CreateDeviceAndSwapChain);
		DetourTransactionCommit();

		if (!bRes) {
			wsprintf(msg, TEXT("Cannot remove hook, code: %d\n"), GetLastError());
			MessageBox(hwnd, TEXT("Cannot remove hook.\n"), TEXT("error"), MB_ICONERROR);
		}
	}
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
		
		break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

