// EVECap.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#define WIN32_LEAN_AND_MEAN

#pragma comment( lib, "user32.lib") 
#pragma comment( lib, "gdi32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "detours.lib")
#pragma comment(lib, "d3d11.lib")


#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include <Psapi.h>
#include <detours.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <strsafe.h>
#include <D3D11.h>
#include <fstream>

#include "Helpers.h"
#include "resource.h"

using namespace std;



int(__stdcall* origClearRenderTargetView)(ID3D11DeviceContext* DeviceContext, ID3D11RenderTargetView* TargetView, const FLOAT ColorRGBA[4]);
int(__stdcall* origPresent)(IDXGISwapChain* SwapChain, UINT _ui1, UINT _ui2);
int(__stdcall* origDraw)(UINT VertexCount, UINT StartVertexLocation);
int(__stdcall* origDrawIndexed)(ID3D11Device* g_Device, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation);
void ChangeState(char *state);

ID3D11DeviceContext* g_DeviceContext;
ID3D11Device*        g_Device;
ID3D11RenderTargetView* g_RenderTargetView;
ID3D11Texture2D* g_BackBuffer;

HMODULE hD3D11 = NULL;
HMODULE hDXGI = NULL;
static HINSTANCE hHookDLL = NULL;
HINSTANCE hHookInstance = NULL; 

typedef void (CALLBACK *LPINSTALLHOOK)(HWND hWnd, const char *pName, HINSTANCE hins);
typedef void (CALLBACK *LPRELEASEHOOK)();

LPINSTALLHOOK installHook = NULL;


int WINAPI myDraw(UINT VertexCount, UINT StartVertexLocation)
{
	return origDraw(VertexCount, StartVertexLocation);
}


int WINAPI myDrawIndexed(ID3D11Device* g_Device, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
	/*if (myfile)
	{
		sprintf(sBuffer, "%d\n", IndexCount);
		myfile << sBuffer;
	}*/

	if (IndexCount == 57600)
	{
		ChangeState(( char *)"Wireframe");
	}
	else
	{
		ChangeState(( char *)"Solid");
	}

	return origDrawIndexed(g_Device, IndexCount, StartIndexLocation, BaseVertexLocation);
}

int WINAPI myClearRenderTargetView(ID3D11DeviceContext* DeviceContext, ID3D11RenderTargetView* TargetView, const FLOAT ColorRGBA[4])
{
	if (g_DeviceContext == NULL)
	{
		g_DeviceContext = DeviceContext; //save as a global
	}

	if (g_Device == NULL)
	{
		DeviceContext->GetDevice(&g_Device);
	}

	return origClearRenderTargetView(DeviceContext, TargetView, ColorRGBA);
}

int WINAPI myPresent(IDXGISwapChain* SwapChain, UINT _ui1, UINT _ui2)
{
	return origPresent(SwapChain, _ui1, _ui2);
}


void XTrace0(LPCSTR lpszText)
{
	::OutputDebugStringA(lpszText);
}

void XTrace(LPCSTR lpszFormat, ...)
{
	va_list args;
	va_start(args, lpszFormat);
	int nBuf;
	char szBuffer[512]; // get rid of this hard-coded buffer
	nBuf = vsnprintf(szBuffer, 511, lpszFormat, args);
	::OutputDebugStringA(szBuffer);
	va_end(args);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

void ChangeState(char *state)
{
	ID3D11RasterizerState * rState;
	D3D11_RASTERIZER_DESC rDesc;

	// cd3d is the ID3D11DeviceContext  
	g_DeviceContext->RSGetState(&rState); // retrieve the current state
	rState->GetDesc(&rDesc);    // get the desc of the state
	if (_strcmpi(state, "Wireframe") == 0)
	{
		rDesc.FillMode = D3D11_FILL_WIREFRAME; // change the ONE setting
	}
	if (_strcmpi(state, "Solid") == 0)
	{
		rDesc.FillMode = D3D11_FILL_SOLID; // change the ONE setting
	}
	// create a whole new rasterizer state
	// d3d is the ID3D11Device
	g_Device->CreateRasterizerState(&rDesc, &rState);

	g_DeviceContext->RSSetState(rState);    // set the new rasterizer state
}



int WINAPI WinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int nCmdShow)
{
	HWND hWnd;
	WNDCLASSEX wc;
	MSG msg;
	HRESULT hr = NULL;
	int Addr_ClearRenderTargetView = 0;
	int Addr_Present = 0;
	int Addr_Draw = 0;
	int Addr_DrawIndexed = 0;

	MODULEINFO Info_D3D11 = { 0 };
	MODULEINFO Info_DXGI = { 0 };
	hHookInstance = hInstance;
	ZeroMemory(&wc, sizeof(WNDCLASSEX));

	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
	wc.lpszClassName = L"EVECapWindowClass";
	wc.lpszMenuName = MAKEINTRESOURCE(IDR_MENU1);
	

	RegisterClassEx(&wc);

	hWnd = CreateWindowEx(NULL,
		L"EVECapWindowClass",    // name of the window class
		L"EVECap",   // title of the window
		WS_OVERLAPPEDWINDOW,    // window style
		300,    // x-position of the window
		300,    // y-position of the window
		500,    // width of the window
		400,    // height of the window
		NULL,    // we have no parent window, NULL
	    NULL,  // we aren't using menus, NULL
		hInstance,    // application handle
		NULL);    // used with multiple windows, NULL
	
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);


	hHookDLL = LoadLibrary(TEXT("EveHook.dll"));

	if (hHookDLL == NULL)
	{
		XTrace("failed to load EveHook\n");
	}
	else
	{
		XTrace("loaded EveHook.Dll\n");
	}


	while (TRUE)
	{
		// Check to see if any messages are waiting in the queue
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			// Translate the message and dispatch it to WindowProc()
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		// If the message is WM_QUIT, exit the while loop
	  if (msg.message == WM_QUIT)
		{
			
		  break;

		} 

		
		// Run game code here
		// ...
		// ...
	}
	// return this part of the WM_QUIT message to Windows
	return msg.wParam;
}

static void Uninstall_Hook()
{
	if (installHook != NULL)
	{
		XTrace("UnHook()\n");
		((LPRELEASEHOOK)GetProcAddress(hHookDLL, "ReleaseHook"))();
		installHook = FALSE;
	}
}

LRESULT  CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	// sort through and find what code to run for the message given
	switch (message)
	{
		// this message is read when the window is closed
		case WM_COMMAND:

			switch (LOWORD(wParam))
			{
				case ID_FILE_HOOK:
					
				 
					if (installHook == NULL)
					{
						installHook = (LPINSTALLHOOK)GetProcAddress(hHookDLL, "InstallHook");
						if (installHook == NULL)
						{
							XTrace("GetLastError: %s\n", GetLastError());
						}
						else
						{
							installHook(hWnd, "exefile.exe", hHookDLL);
							XTrace("Hook()\n");
						}
					}
					
				
					break;

				case ID_FILE_UNHOOK:
					Uninstall_Hook();
					break;

				case ID_FILE_EXIT:
					XTrace("Exit\n");
					Uninstall_Hook();
					DestroyWindow(hWnd);
					FreeModule(hHookDLL);

					return 0;

				default:
					break;
			}
			

			return 0;

		case WM_CLOSE:
			DestroyWindow(hWnd);
			FreeModule(hHookDLL);
			return 0;

		case WM_DESTROY:
		{
			Uninstall_Hook();
			FreeModule(hHookDLL);
			PostQuitMessage(0);
			return 0;
		} 
			break;

		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
	}

	// Handle any messages the switch statement didn't
	
	return NULL;
}