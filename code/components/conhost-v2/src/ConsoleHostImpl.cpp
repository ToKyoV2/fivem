/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#include "StdInc.h"
#include <Error.h>
#include "ConsoleHost.h"
#include "ConsoleHostImpl.h"

#include <d3d11.h>

#include <mutex>
#include <queue>

#include <sstream>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>

#ifndef IS_LAUNCHER
#include <DrawCommands.h>
#include <grcTexture.h>

#include <InputHook.h>
#endif

#include <mmsystem.h>

#include <imgui.h>

#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

#include <ShlObj.h>

#include <HostSharedData.h>
#include <ReverseGameData.h>

static bool g_conHostInitialized = false;
extern bool g_consoleFlag;
extern bool g_cursorFlag;
int g_scrollTop;
int g_bufferHeight;

#ifndef IS_LAUNCHER
static uint32_t g_pointSamplerState;
static rage::grcTexture* g_fontTexture;

static void RenderDrawListInternal(ImDrawList* drawList, ImDrawData* refData)
{
#ifndef _HAVE_GRCORE_NEWSTATES
	SetRenderState(0, grcCullModeNone);
	SetRenderState(2, 0); // alpha blending m8

	GetD3D9Device()->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	GetD3D9Device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	GetD3D9Device()->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
#else
	auto oldRasterizerState = GetRasterizerState();
	SetRasterizerState(GetStockStateIdentifier(RasterizerStateNoCulling));

	auto oldBlendState = GetBlendState();
	SetBlendState(GetStockStateIdentifier(BlendStateDefault));

	auto oldDepthStencilState = GetDepthStencilState();
	SetDepthStencilState(GetStockStateIdentifier(DepthStencilStateNoDepth));
#endif

	size_t idxOff = 0;

	for (auto& cmd : drawList->CmdBuffer)
	{
		if (cmd.UserCallback != nullptr)
		{
			cmd.UserCallback(nullptr, &cmd);
		}
		else
		{
			if (cmd.TextureId)
			{
				SetTextureGtaIm(*(rage::grcTexture**)cmd.TextureId);
			}
			else
			{
				SetTextureGtaIm(rage::grcTextureFactory::GetNoneTexture());
			}

			PushDrawBlitImShader();

			for (int s = 0; s < cmd.ElemCount; s += 2046)
			{
				int c = std::min(cmd.ElemCount - s, uint32_t(2046));
				rage::grcBegin(3, c);

				//trace("imgui draw %d tris\n", cmd.ElemCount);

				for (int i = 0; i < c; i++)
				{
					auto& vertex = drawList->VtxBuffer.Data[drawList->IdxBuffer.Data[i + idxOff]];
					auto color = vertex.col;
					
					if (!rage::grcTexture::IsRenderSystemColorSwapped())
					{
						color = (color & 0xFF00FF00) | _rotl(color & 0x00FF00FF, 16);
					}

					rage::grcVertex(vertex.pos.x - refData->DisplayPos.x, vertex.pos.y - refData->DisplayPos.y, 0.0f, 0.0f, 0.0f, -1.0f, color, vertex.uv.x, vertex.uv.y);
				}

				idxOff += c;

#if defined(GTA_FIVE)
				// set scissor rects here, as they might be overwritten by a matrix push
				D3D11_RECT scissorRect;
				scissorRect.left = cmd.ClipRect.x - refData->DisplayPos.x;
				scissorRect.top = cmd.ClipRect.y - refData->DisplayPos.y;
				scissorRect.right = cmd.ClipRect.z - refData->DisplayPos.x;
				scissorRect.bottom = cmd.ClipRect.w - refData->DisplayPos.y;

				GetD3D11DeviceContext()->RSSetScissorRects(1, &scissorRect);
#else
				SetScissorRect(cmd.ClipRect.x - refData->DisplayPos.x, cmd.ClipRect.y - refData->DisplayPos.y, cmd.ClipRect.z - refData->DisplayPos.x, cmd.ClipRect.w - refData->DisplayPos.y);
#endif

				rage::grcEnd();
			}

			PopDrawBlitImShader();
		}
	}

#ifdef _HAVE_GRCORE_NEWSTATES
	SetRasterizerState(oldRasterizerState);

	SetBlendState(oldBlendState);

	SetDepthStencilState(oldDepthStencilState);
#else
	GetD3D9Device()->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_ANISOTROPIC);
	GetD3D9Device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
	GetD3D9Device()->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
#endif

	delete drawList;

#if defined(GTA_FIVE)
	{
		D3D11_RECT scissorRect;
		scissorRect.left = 0.0f;
		scissorRect.top = 0.0f;
		scissorRect.right = 1.0f;
		scissorRect.bottom = 1.0f;

		GetD3D11DeviceContext()->RSSetScissorRects(1, &scissorRect);
	}
#else
	SetScissorRect(0, 0, 0x1FFF, 0x1FFF);
#endif
}

static void RenderDrawLists(ImDrawData* drawData)
{
	if (!drawData->Valid)
	{
		return;
	}

	for (int i = 0; i < drawData->CmdListsCount; i++)
	{
		ImDrawList* drawList = drawData->CmdLists[i];
		ImDrawList* grDrawList = drawList->CloneOutput();

		if (IsOnRenderThread())
		{
			RenderDrawListInternal(grDrawList, drawData);
		}
		else
		{
			uintptr_t argRef = (uintptr_t)grDrawList;
			uintptr_t argRefB = (uintptr_t)drawData;

			EnqueueGenericDrawCommand([](uintptr_t a, uintptr_t b)
			{
				RenderDrawListInternal((ImDrawList*)a, (ImDrawData*)b);
			},
			&argRef, &argRefB);
		}
	}
}

static void CreateFontTexture()
{
	ImGuiIO& io = ImGui::GetIO();
	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	rage::grcTextureReference reference;
	memset(&reference, 0, sizeof(reference));
	reference.width = width;
	reference.height = height;
	reference.depth = 1;
	reference.stride = width * 4;
#ifdef GTA_NY
	reference.format = 1;
#else
	reference.format = 11;
#endif
	reference.pixelData = (uint8_t*)pixels;

	rage::grcTexture* texture = rage::grcTextureFactory::getInstance()->createImage(&reference, nullptr);
	g_fontTexture = texture;

	if (!io.Fonts->TexID)
	{
		void** texId = new void*[2];
		io.Fonts->TexID = (ImTextureID)texId;
	}

	((void**)io.Fonts->TexID)[0] = (void*)g_fontTexture;
}
#else
DLL_EXPORT fwEvent<ImDrawData*> OnRenderImDrawData;

static void RenderDrawLists(ImDrawData* drawData)
{
	if (!drawData->Valid)
	{
		return;
	}

	OnRenderImDrawData(drawData);
}
#endif

void DrawConsole();
void DrawDevGui();

static std::mutex g_conHostMutex;
ImFont* consoleFontSmall;

void DrawMiniConsole();

static void HandleFxDKInput(ImGuiIO& io)
{
	static HostSharedData<ReverseGameData> rgd("CfxReverseGameData");
	static uint16_t lastInputChar = 0;
	static uint32_t inputCharChangedAt = timeGetTime();

	auto currentInputChar = rgd->inputChar;
	auto inputCharChanged = currentInputChar != lastInputChar;

	lastInputChar = currentInputChar;

	if (currentInputChar > 0 && currentInputChar < 0x10000)
	{
		if (inputCharChanged)
		{
			inputCharChangedAt = timeGetTime();
			io.AddInputCharacterUTF16(currentInputChar);
		}
		else if (timeGetTime() - inputCharChangedAt > 300)
		{
			io.AddInputCharacterUTF16(currentInputChar);
		}
	}
}

DLL_EXPORT void OnConsoleFrameDraw(int width, int height)
{
	if (!g_conHostMutex.try_lock())
	{
		return;
	}

#ifndef IS_LAUNCHER
	if (!g_fontTexture)
	{
		CreateFontTexture();
	}
#endif

	static uint32_t lastDrawTime = timeGetTime();

	bool shouldDrawGui = false;

	ConHost::OnShouldDrawGui(&shouldDrawGui);

	if (!g_cursorFlag && !g_consoleFlag && !shouldDrawGui)
	{
		lastDrawTime = timeGetTime();

		g_conHostMutex.unlock();
		return;
	}

	ImGuiIO& io = ImGui::GetIO();

	HandleFxDKInput(io);

	io.MouseDrawCursor = g_consoleFlag || g_cursorFlag;

	{
		io.DisplaySize = ImVec2(width, height);

		io.DeltaTime = (timeGetTime() - lastDrawTime) / 1000.0f;
	}

	io.KeyCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	io.KeyShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	io.KeyAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
	io.KeySuper = false;

	if (ImGui::GetPlatformIO().Viewports.Size > 1)
	{
		MSG msg = { 0 };

		while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	ImGui::NewFrame();

	bool wasSmallFont = false;

	{
		int x, y;
		GetGameResolution(x, y);

		if (x <= 1920 && y <= 1080)
		{
			ImGui::PushFont(consoleFontSmall);
			wasSmallFont = true;
		}
	}

	if (g_consoleFlag)
	{
		DrawDevGui();
		DrawConsole();
	}

	DrawMiniConsole();

	ConHost::OnDrawGui();

	if (wasSmallFont)
	{
		ImGui::PopFont();
	}

	ImGui::Render();
	RenderDrawLists(ImGui::GetDrawData());

    ImGui::UpdatePlatformWindows();
	ImGui::RenderPlatformWindowsDefault();

	lastDrawTime = timeGetTime();

	g_conHostMutex.unlock();
}

static void OnConsoleWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, bool& pass, LRESULT& result)
{
	bool shouldDrawGui = false;

	ConHost::OnShouldDrawGui(&shouldDrawGui);

	if ((!g_consoleFlag && !g_cursorFlag) || !pass)
	{
		return;
	}

	std::unique_lock<std::mutex> g_conHostMutex;
	ImGuiIO& io = ImGui::GetIO();

#if 0
	switch (msg)
	{
		case WM_LBUTTONDOWN:
			io.MouseDown[0] = true;
			pass = false;
			break;
		case WM_LBUTTONUP:
			io.MouseDown[0] = false;
			pass = false;
			break;
		case WM_RBUTTONDOWN:
			io.MouseDown[1] = true;
			pass = false;
			break;
		case WM_RBUTTONUP:
			io.MouseDown[1] = false;
			pass = false;
			break;
		case WM_MBUTTONDOWN:
			io.MouseDown[2] = true;
			pass = false;
			break;
		case WM_MBUTTONUP:
			io.MouseDown[2] = false;
			pass = false;
			break;
		case WM_MOUSEWHEEL:
			io.MouseWheel += GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? +1.0f : -1.0f;
			pass = false;
			break;
		case WM_MOUSEMOVE:
			io.MousePos.x = (signed short)(lParam);
			io.MousePos.y = (signed short)(lParam >> 16);
			pass = false;
			break;
		case WM_KEYDOWN:
			if (wParam < 256)
				io.KeysDown[wParam] = 1;
			pass = false;
			break;
		case WM_KEYUP:
			if (wParam < 256)
				io.KeysDown[wParam] = 0;
			pass = false;
			break;
		case WM_CHAR:
			// You can also use ToAscii()+GetKeyboardState() to retrieve characters.
			if (wParam > 0 && wParam < 0x10000)
				io.AddInputCharacter((unsigned short)wParam);
			pass = false;
			break;
		case WM_INPUT:
			pass = false;
			break;
	}
#endif

	ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
	
	if (msg == WM_INPUT || (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST))
	{
		pass = false;
	}

	if (g_consoleFlag && ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) || msg == WM_CHAR))
	{
		pass = false;
	}

	if (!pass)
	{
		result = true;
	}
}

void ProcessWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, bool& pass, LRESULT& lresult);

DLL_EXPORT void RunConsoleWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, bool& pass, LRESULT& result)
{
	ProcessWndProc(hWnd, msg, wParam, lParam, pass, result);
	OnConsoleWndProc(hWnd, msg, wParam, lParam, pass, result);
}

ImFont* consoleFontTiny;

#pragma comment(lib, "d3d11.lib")

void ImGui_ImplWin32_InitPlatformInterface();

static HookFunction initFunction([]()
{
	auto cxt = ImGui::CreateContext();
	ImGui::SetCurrentContext(cxt);

	ImGuiIO& io = ImGui::GetIO();
	io.KeyMap[ImGuiKey_Tab] = VK_TAB; // Keyboard mapping. ImGui will use those indices to peek into the io.KeyDown[] array that we will update during the application lifetime.
	io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
	io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
	io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
	io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
	io.KeyMap[ImGuiKey_PageUp] = VK_PRIOR;
	io.KeyMap[ImGuiKey_PageDown] = VK_NEXT;
	io.KeyMap[ImGuiKey_Home] = VK_HOME;
	io.KeyMap[ImGuiKey_End] = VK_END;
	io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
	io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
	io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
	io.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
	io.KeyMap[ImGuiKey_A] = 'A';
	io.KeyMap[ImGuiKey_C] = 'C';
	io.KeyMap[ImGuiKey_V] = 'V';
	io.KeyMap[ImGuiKey_X] = 'X';
	io.KeyMap[ImGuiKey_Y] = 'Y';
	io.KeyMap[ImGuiKey_Z] = 'Z';

	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigDockingWithShift = true;
	io.ConfigWindowsResizeFromEdges = true;

	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

	io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos; // We can honor io.WantSetMousePos requests (optional, rarely used)
	io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports; // We can create multi-viewports on the Platform side (optional)
	io.BackendFlags |= ImGuiBackendFlags_HasMouseHoveredViewport; // We can set io.MouseHoveredViewport correctly (optional, not easy)

	io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;

	ImGuiViewport* main_viewport = ImGui::GetMainViewport();
	main_viewport->PlatformHandle = main_viewport->PlatformHandleRaw = nullptr;

	ID3D11Device* device;
	ID3D11DeviceContext* immcon;

	// use the system function as many proxy DLLs don't like multiple devices being made in the game process
	// and they're 'closed source' and 'undocumented' so we can't reimplement the same functionality natively

	// also, create device here and not after the game's or nui:core hacks will mismatch with proxy DLLs
	wchar_t systemD3D11Name[512];
	GetSystemDirectoryW(systemD3D11Name, std::size(systemD3D11Name));
	wcscat(systemD3D11Name, L"\\d3d11.dll");

	auto systemD3D11 = LoadLibraryW(systemD3D11Name);
	assert(systemD3D11);

	auto systemD3D11CreateDevice = (decltype(&D3D11CreateDevice))GetProcAddress(systemD3D11, "D3D11CreateDevice");

	systemD3D11CreateDevice(NULL,
	D3D_DRIVER_TYPE_HARDWARE,
	nullptr,
	0,
	nullptr,
	0,
	D3D11_SDK_VERSION,
	&device,
	nullptr,
	&immcon);

	OnGrcCreateDevice.Connect([io, device, immcon]()
	{
		if (device)
		{
			if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			{
				ImGui_ImplWin32_InitPlatformInterface();
			}

			ImGui_ImplDX11_Init(device, immcon);
		}
	});

	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

	static std::string imguiIni = ([]
	{
		std::wstring outPath = MakeRelativeCitPath(L"citizen/imgui.ini");

		PWSTR appDataPath;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataPath)))
		{
			// create the directory if not existent
			std::wstring cfxPath = std::wstring(appDataPath) + L"\\CitizenFX";
			CreateDirectory(cfxPath.c_str(), nullptr);

			std::wstring profilePath = cfxPath + L"\\imgui.ini";

			if (GetFileAttributes(profilePath.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributes(outPath.c_str()) != INVALID_FILE_ATTRIBUTES)
			{
				CopyFile(outPath.c_str(), profilePath.c_str(), FALSE);
			}

			CoTaskMemFree(appDataPath);

			outPath = profilePath;
		}

		return ToNarrow(outPath);
	})();

	io.IniFilename = const_cast<char*>(imguiIni.c_str());
	//io.ImeWindowHandle = g_hWnd;

	ImGuiStyle& style = ImGui::GetStyle();

	ImColor hiliteBlue = ImColor(81, 179, 236);
	ImColor hiliteBlueTransparent = ImColor(81, 179, 236, 180);
	ImColor backgroundBlue = ImColor(22, 24, 28, 200);
	ImColor semiTransparentBg = ImColor(50, 50, 50, 0.6 * 255);
	ImColor semiTransparentBgHover = ImColor(80, 80, 80, 0.6 * 255);

	style.Colors[ImGuiCol_WindowBg] = backgroundBlue;
	style.Colors[ImGuiCol_TitleBg] = hiliteBlue;
	style.Colors[ImGuiCol_TitleBgActive] = hiliteBlue;
	style.Colors[ImGuiCol_TitleBgCollapsed] = hiliteBlue;
	style.Colors[ImGuiCol_Border] = hiliteBlue;
	style.Colors[ImGuiCol_FrameBg] = semiTransparentBg;
	style.Colors[ImGuiCol_FrameBgHovered] = semiTransparentBgHover;
	style.Colors[ImGuiCol_TextSelectedBg] = hiliteBlueTransparent;

	// fuck rounding
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

	{
		FILE* font = _wfopen(MakeRelativeCitPath(L"citizen/mensch.ttf").c_str(), L"rb");

		ImVector<ImWchar> ranges;
		ImFontGlyphRangesBuilder builder;

		static const ImWchar extra_ranges[] =
		{
			0x0100, 0x017F, // Latin Extended-A
			0x0180, 0x024F, // Latin Extended-B
			0x0370, 0x03FF, // Greek and Coptic
			0x10A0, 0x10FF, // Georgian
			0x1E00, 0x1EFF, // Latin Extended Additional
			0,
		};

		builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
		builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
		builder.AddRanges(&extra_ranges[0]);
		builder.BuildRanges(&ranges);

		if (font)
		{
			fseek(font, 0, SEEK_END);

			auto fontSize = ftell(font);

			uint8_t* fontData = new uint8_t[fontSize];

			fseek(font, 0, SEEK_SET);

			fread(&fontData[0], 1, fontSize, font);
			fclose(font);

			io.Fonts->AddFontFromMemoryTTF(fontData, fontSize, 22.0f, NULL, ranges.Data);

			consoleFontSmall = io.Fonts->AddFontFromMemoryTTF(fontData, fontSize, 18.0f, NULL, ranges.Data);
			consoleFontTiny = io.Fonts->AddFontFromMemoryTTF(fontData, fontSize, 14.0f, NULL, ranges.Data);
			io.Fonts->Build();
		}
	}

#ifndef IS_LAUNCHER
	OnGrcCreateDevice.Connect([=]()
	{
#ifdef _HAVE_GRCORE_NEWSTATES
#ifndef IS_RDR3
		D3D11_SAMPLER_DESC samplerDesc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.MaxAnisotropy = 0;

		g_pointSamplerState = CreateSamplerState(&samplerDesc);
#else
		g_pointSamplerState = GetStockStateIdentifier(SamplerStatePoint);
#endif
#endif
	});

	InputHook::QueryInputTarget.Connect([](std::vector<InputTarget*>& targets)
	{
		if (!g_consoleFlag && !g_cursorFlag)
		{
			return true;
		}

		static struct : InputTarget
		{
			virtual inline void KeyDown(UINT vKey, UINT scanCode) override
			{
				std::unique_lock<std::mutex> g_conHostMutex;
				
				ImGuiIO& io = ImGui::GetIO();

				if (vKey < 256)
				{
					io.KeysDown[vKey] = 1;
				}
			}

			virtual inline void KeyUp(UINT vKey, UINT scanCode) override
			{
				std::unique_lock<std::mutex> g_conHostMutex;

				ImGuiIO& io = ImGui::GetIO();

				if (vKey < 256)
				{
					io.KeysDown[vKey] = 0;
				}
			}

			virtual inline void MouseDown(int buttonIdx, int x, int y) override
			{
				std::unique_lock<std::mutex> g_conHostMutex;

				ImGuiIO& io = ImGui::GetIO();

				if (buttonIdx < std::size(io.MouseDown))
				{
					io.MouseDown[buttonIdx] = true;
				}
			}

			virtual inline void MouseUp(int buttonIdx, int x, int y) override
			{
				std::unique_lock<std::mutex> g_conHostMutex;

				ImGuiIO& io = ImGui::GetIO();

				if (buttonIdx < std::size(io.MouseDown))
				{
					io.MouseDown[buttonIdx] = false;
				}
			}

			virtual inline void MouseWheel(int delta) override
			{
				if (delta == 0)
				{
					return;
				}

				std::unique_lock<std::mutex> g_conHostMutex;

				ImGuiIO& io = ImGui::GetIO();
				io.MouseWheel += delta > 0 ? +1.0f : -1.0f;
			}

			virtual inline void MouseMove(int x, int y) override
			{
				std::unique_lock<std::mutex> g_conHostMutex;

				ImGuiIO& io = ImGui::GetIO();
				io.MousePos.x = (signed short)(x);
				io.MousePos.y = (signed short)(y);
			}
		} tgt;

		targets.push_back(&tgt);

		return false;
	});

	InputHook::DeprecatedOnWndProc.Connect([](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, bool& pass, LRESULT& lresult)
	{
		OnConsoleWndProc(hWnd, msg, wParam, lParam, pass, lresult);
	});

	InputHook::QueryMayLockCursor.Connect([](int& may)
	{
		if (g_consoleFlag || g_cursorFlag)
		{
			may = 0;
		}
	});

	OnPostFrontendRender.Connect([]()
	{
		int width, height;
		GetGameResolution(width, height);

		OnConsoleFrameDraw(width, height);
	});
#endif
});

struct ConsoleKeyEvent
{
	uint32_t vKey;
	wchar_t character;
	ConsoleModifiers modifiers;
};

void SendPrintMessage(const std::string& channel, const std::string& message);

bool ConHost::IsConsoleOpen()
{
	return g_consoleFlag;
}

void ConHost::Print(const std::string& channel, const std::string& message)
{
	SendPrintMessage(channel, message);
}

fwEvent<const char*, const char*> ConHost::OnInvokeNative;
fwEvent<> ConHost::OnDrawGui;
fwEvent<bool*> ConHost::OnShouldDrawGui;

DLL_EXPORT ImFont* GetConsoleFontSmall()
{
	return consoleFontSmall;
}

DLL_EXPORT ImFont* GetConsoleFontTiny()
{
	return consoleFontTiny;
}

// GTA-specific
#include <Hooking.h>

static decltype(&ReleaseCapture) g_origReleaseCapture;

static void WINAPI ReleaseCaptureStub()
{
	if (g_consoleFlag || g_cursorFlag)
	{
		return;
	}

	g_origReleaseCapture();
}

static HookFunction hookFunction([]()
{
	g_origReleaseCapture = (decltype(g_origReleaseCapture))hook::iat("user32.dll", ReleaseCaptureStub, "ReleaseCapture");
});
