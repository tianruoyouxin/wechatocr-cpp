#include "stdafx.h"
#include "wechatocr.h"
#include <google/protobuf/stubs/common.h>

// dllmain
int APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

static string json_encode(const string& input) {
	string output;
	output.reserve(input.size() + 2);
	for (auto c : input) {
		switch (c) {
		case '"': output += "\\\"";	break;
		case '\\': output += "\\\\"; break;
		case '\n': output += "\\n"; break;
		case '\r': output += "\\r";	break;
		case '\t': output += "\\t";	break;
		default:
			if (c>=0 && c < 0x20) {
				char buf[16];
				snprintf(buf, sizeof(buf), "\\u%04x", c);
				output += buf;
			} else {
				output.push_back(c);
			}
		}
	}
	return output;
}

static std::unique_ptr<CWeChatOCR> g_instance;

extern "C" __declspec(dllexport)
bool wechat_ocr(const wchar_t* ocr_exe, const wchar_t * wechat_dir, const char * imgfn, void(*set_res)(const char * dt))
{
	if (!g_instance) {
		auto ocr = std::make_unique<CWeChatOCR>(ocr_exe, wechat_dir);
		if (!ocr->wait_connection(5000)) {
			return false;
		}
		g_instance = std::move(ocr);
	}
	CWeChatOCR::result_t res;
	if (!g_instance->doOCR(imgfn, &res))
		return false;
	std::string json;
	json += "{\n";
	json += "  \"lines\": [\n";

	for (size_t i = 0; i < res.ocr_response.size(); ++i) {
		const auto& blk = res.ocr_response[i];

		json += "    {\n";
		json += "      \"text\": \"" + json_encode(blk.text) + "\",\n";

		// bbox 顺序: TopLeft, TopRight, BottomRight, BottomLeft
		json += "      \"bbox\": ["
			+ std::to_string(std::lround(blk.left)) + ","    // TopLeft.X
			+ std::to_string(std::lround(blk.top)) + ","     // TopLeft.Y
			+ std::to_string(std::lround(blk.right)) + ","   // TopRight.X
			+ std::to_string(std::lround(blk.top)) + ","     // TopRight.Y
			+ std::to_string(std::lround(blk.right)) + ","   // BottomRight.X
			+ std::to_string(std::lround(blk.bottom)) + ","  // BottomRight.Y
			+ std::to_string(std::lround(blk.left)) + ","    // BottomLeft.X
			+ std::to_string(std::lround(blk.bottom)) +      // BottomLeft.Y
			"]\n";

		json += "    }";
		if (i + 1 < res.ocr_response.size()) json += ",";
		json += "\n";
	}
	json += "  ]\n";
	json += "}\n";
	if (set_res) {
		set_res(json.c_str());
	}
	return true;
}






//extern "C" __declspec(dllexport)
//void stop_ocr() {
//	if(g_instance)
//	g_instance.reset();
//}

extern "C" __declspec(dllexport) void __stdcall stop_ocr() {
	if (g_instance) {
		g_instance.reset();
	}
}


#include "Windows.h"
inline std::basic_string<wchar_t> getExeDirOrt() {
	wchar_t buf[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
	std::wstring full = n ? std::wstring(buf, buf + n) : L".";
	size_t pos = full.find_last_of(L"/\\");
	return (pos == std::wstring::npos) ? L"." : full.substr(0, pos);
}
inline std::string ortToUtf8(const std::basic_string<wchar_t>& s) {
	if (s.empty()) return {};
	int needed = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
		nullptr, 0, nullptr, nullptr);
	std::string out(needed, '\0');
	WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
		out.data(), needed, nullptr, nullptr);
	return out;
}
inline std::wstring utf8ToWstring(const std::string& s) {
	if (s.empty()) return {};

	// 先计算需要的 wchar_t 数量
	int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
	if (needed <= 0) return {};

	std::wstring out(needed, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), needed);
	return out;
}

#include <shlwapi.h>
#include <string>
#include <filesystem>
#include <vector>
#include <iostream>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Version.lib")  // ✅ 关键：链接 version API
std::wstring GetEnvVar(const wchar_t* name) {
	wchar_t* value = nullptr;
	size_t len = 0;

	if (_wdupenv_s(&value, &len, name) == 0 && value != nullptr) {
		std::wstring result(value);
		free(value);  // 必须释放内存
		return result;
	}
	return L"";
}
namespace fs = std::filesystem;

std::pair<std::wstring, std::wstring> FindWeChat() {
	HKEY hKey;
	std::wstring installPath=L"";
	if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Tencent\\WeChat", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
		// 如果 WeChat 注册表键打开失败，则尝试打开 Weixin 键
		if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Tencent\\Weixin", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
		}
	}
	WCHAR buffer[MAX_PATH];
	DWORD size = sizeof(buffer);
	if (RegQueryValueExW(hKey, L"InstallPath", nullptr, nullptr, (LPBYTE)buffer, &size) != ERROR_SUCCESS) {
		RegCloseKey(hKey);
	}
	RegCloseKey(hKey);
	if (installPath == L"")
	{
		std::wstring paths[] = {
			L"C:\\Program Files (x86)\\Tencent\\Weixin",
			L"C:\\Program Files\\Tencent\\Weixin",
			L"C:\\Program Files (x86)\\Tencent\\WeChat",
			L"C:\\Program Files\\Tencent\\WeChat"
		};
		for (const auto& path : paths) {

			if (fs::is_directory(path)) {  // 确保是文件夹
				installPath = path;  // 返回第一个存在的路径
				break;
			}
		}
	}
	else
	{
		installPath = buffer;
	}

	fs::path base = installPath;
	fs::path exePath = base / L"WeChat.exe";
	if (!fs::exists(exePath)) {
		exePath = base / L"Weixin.exe";
	}

	if (!fs::exists(exePath)) {
		throw std::runtime_error("未找到微信主程序");
	}

	DWORD handle = 0;
	DWORD verSize = GetFileVersionInfoSizeW(exePath.c_str(), &handle);
	if (verSize == 0) {
		throw std::runtime_error("GetFileVersionInfoSizeW 失败");
	}

	std::vector<BYTE> verData(verSize);
	std::wstring versionString;

	if (GetFileVersionInfoW(exePath.c_str(), handle, verSize, verData.data())) {
		VS_FIXEDFILEINFO* verInfo = nullptr;
		UINT sizeInfo = 0;
		if (VerQueryValueW(verData.data(), L"\\", (LPVOID*)&verInfo, &sizeInfo)) {
			int major = HIWORD(verInfo->dwFileVersionMS);
			int minor = LOWORD(verInfo->dwFileVersionMS);
			int build = HIWORD(verInfo->dwFileVersionLS);
			int rev = LOWORD(verInfo->dwFileVersionLS);

			versionString = std::to_wstring(major) + L"." +
				std::to_wstring(minor) + L"." +
				std::to_wstring(build) + L"." +
				std::to_wstring(rev);

			std::wstring appdata = GetEnvVar(L"APPDATA");
			fs::path wechatPath;
			fs::path wechatOcrPath;

			if (major == 4) {
				wechatPath = base / versionString;
				wechatOcrPath = fs::path(appdata) / L"Tencent\\xwechat\\XPlugin\\plugins\\WeChatOcr";

				if (fs::exists(wechatOcrPath)) {
					for (auto& entry : fs::directory_iterator(wechatOcrPath)) {
						if (entry.is_directory()) {
							wechatOcrPath = entry.path() / L"extracted\\wxocr.dll";
							break;
						}
					}
				}
			}
			else if (major == 3) {
				wechatPath = base / (L"[" + versionString + L"]");
				wechatOcrPath = fs::path(appdata) / L"Tencent\\WeChat\\XPlugin\\Plugins\\WeChatOCR";

				if (fs::exists(wechatOcrPath)) {
					for (auto& entry : fs::directory_iterator(wechatOcrPath)) {
						if (entry.is_directory()) {
							wechatOcrPath = entry.path() / L"extracted\\WeChatOCR.exe";
							break;
						}
					}
				}
			}

			return { wechatOcrPath.wstring(), wechatPath.wstring() };
		}
	}

	throw std::runtime_error("无法读取版本号");
}



int main(int argc, char* argv[]) {
	SetConsoleOutputCP(CP_UTF8); // 控制台输出 UTF-8
	std::string img_path= "";
	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--img" && i + 1 < argc) {
			img_path = argv[++i];  // 下一个参数是路径
		}
	}
	if (img_path == "")
	{
		return 0;
	}
	auto exe_dir = getExeDirOrt();
	auto [ocrPath, wechatPath] = FindWeChat();
	std::wstring ocr_exe = ocrPath;
	std::wstring wechat_dir = wechatPath;

	if (!g_instance) {
		auto ocr = std::make_unique<CWeChatOCR>(ocr_exe.c_str(), wechat_dir.c_str());
		if (!ocr->wait_connection(5000)) {
			return false;
		}
		g_instance = std::move(ocr);
	}

	CWeChatOCR::result_t res;
	if (!g_instance->doOCR(img_path, &res))
		return false;

	std::string json;
	json += "{\n";
	json += "  \"lines\": [\n";

	for (size_t i = 0; i < res.ocr_response.size(); ++i) {
		const auto& blk = res.ocr_response[i];

		json += "    {\n";
		json += "      \"text\": \"" + json_encode(blk.text) + "\",\n";

		// 直接添加文字块的边框信息
		json += "      \"bbox\": ["
			+ std::to_string(std::lround(blk.left)) + ","    // TopLeft.X
			+ std::to_string(std::lround(blk.top)) + ","     // TopLeft.Y
			+ std::to_string(std::lround(blk.right)) + ","   // TopRight.X
			+ std::to_string(std::lround(blk.top)) + ","     // TopRight.Y
			+ std::to_string(std::lround(blk.right)) + ","   // BottomRight.X
			+ std::to_string(std::lround(blk.bottom)) + ","  // BottomRight.Y
			+ std::to_string(std::lround(blk.left)) + ","    // BottomLeft.X
			+ std::to_string(std::lround(blk.bottom)) +      // BottomLeft.Y
			"]\n";

		json += "    }";
		if (i + 1 < res.ocr_response.size()) json += ",";
		json += "\n";
	}

	json += "  ]\n";
	json += "}\n";

	std::cout << json << std::endl;
	if (g_instance) {
		g_instance.reset();
	}
	//std::cin.get(); // 等待回车
	return 0;
}













#ifdef _DEBUG
// 定义这个函数仅方便使用regsvr32.exe调试本DLL, 使用环境变量WECHATOCR_EXE和WECHAT_DIR以及TEST_PNG传入调试参数
extern "C" __declspec(dllexport)
HRESULT DllRegisterServer(void)
{
	if (AllocConsole()) {
		(void)freopen("CONOUT$", "w", stdout);
		(void)freopen("CONOUT$", "w", stderr);
		// disalbe stdout cache.
		setvbuf(stdout, NULL, _IONBF, 0);
		SetConsoleCP(CP_UTF8);
		SetConsoleOutputCP(CP_UTF8);
	}

	// printf("Protobuf version: %u\n", GOOGLE_PROTOBUF_VERSION);
	// printf("tencent protobuf version string: %s\n", google::protobuf::internal::VersionString(2005000).c_str());
	string msg;

	CWeChatOCR wechat_ocr(_wgetenv(L"WECHATOCR_EXE"), _wgetenv(L"WECHAT_DIR"));
	if (!wechat_ocr.wait_connection(5000)) {
		fprintf(stderr, "wechat_ocr.wait_connection failed\n");
		return E_FAIL;
	}
	wechat_ocr.doOCR(getenv("TEST_PNG"), nullptr);
	wechat_ocr.wait_done(-1);
	fprintf(stderr, "debug play ocr DONE!\n");
	return S_OK;
}
#endif
