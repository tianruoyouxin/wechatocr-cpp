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
	//string json;
	//json += "{";
	//json += "\"errcode\":" + std::to_string(res.errcode) + ",";
	//json += "\"imgpath\":\"" + json_encode(res.imgpath) + "\",";
	//json += "\"width\":" + std::to_string(res.width) + ",";
	//json += "\"height\":" + std::to_string(res.height) + ",";
	//json += "\"ocr_response\":[";
	//for (auto& blk : res.ocr_response) {
	//	json += "{";
	//	json += "\"left\":" + std::to_string(blk.left) + ",";
	//	json += "\"top\":" + std::to_string(blk.top) + ",";
	//	json += "\"right\":" + std::to_string(blk.right) + ",";
	//	json += "\"bottom\":" + std::to_string(blk.bottom) + ",";
	//	json += "\"rate\":" + std::to_string(blk.rate) + ",";
	//	json += "\"text\":\"" + json_encode(blk.text) + "\"";
	//	json += "},";
	//}
	//if (json.back() == ',') {
	//	json.pop_back();
	//}
	//json += "]}";

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


bool TryGetInstallPathFromRegistry(HKEY rootKey, const std::wstring& subKey, std::wstring& outPath) {
	HKEY hKey = nullptr;
	bool success = false;

	// 先尝试64位视图，再尝试32位视图
	if (RegOpenKeyExW(rootKey, subKey.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS ||
		RegOpenKeyExW(rootKey, subKey.c_str(), 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS) {

		WCHAR buffer[MAX_PATH] = { 0 };
		DWORD size = sizeof(buffer);

		// 检查InstallPath是否存在且非空
		if (RegQueryValueExW(hKey, L"InstallPath", nullptr, nullptr,
			reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS &&
			size > 0 && buffer[0] != L'\0') {
			outPath = buffer;
			success = true;
			// std::cout << ortToUtf8(L"从注册表[") << ortToUtf8(subKey)
			//     << ortToUtf8(L"]获取安装路径: ") << ortToUtf8(outPath) << std::endl;
		}
		else {
			// std::cout << ortToUtf8(L"注册表键存在但InstallPath为空或不存在: ")
			//     << ortToUtf8(subKey) << std::endl;
		}

		RegCloseKey(hKey);
	}
	else {
		// std::cout << ortToUtf8(L"无法打开注册表键: ") << ortToUtf8(subKey) << std::endl;
	}

	return success;
}

std::pair<std::wstring, std::wstring> FindWeChat() {
	// 设置控制台输出为UTF-8
	SetConsoleOutputCP(CP_UTF8);

	// std::cout << ortToUtf8(L"开始查找微信安装路径...") << std::endl;

	// 在FindWeChat函数中使用：
	std::wstring installPath = L"";

	// 尝试从WeChat注册表获取
	if (!TryGetInstallPathFromRegistry(HKEY_CURRENT_USER, L"SOFTWARE\\Tencent\\WeChat", installPath)) {
		// std::cout << ortToUtf8(L"尝试从Weixin注册表获取...") << std::endl;
		// 如果WeChat失败，尝试Weixin
		TryGetInstallPathFromRegistry(HKEY_CURRENT_USER, L"SOFTWARE\\Tencent\\Weixin", installPath);
	}

	if (installPath.empty()) {
		// std::cout << ortToUtf8(L"无法从注册表获取有效安装路径") << std::endl;
	}

	// 2. 如果注册表获取失败，尝试默认路径
	if (installPath.empty()) {
		// std::cout << ortToUtf8(L"开始检查默认安装路径...") << std::endl;

		std::vector<std::wstring> paths = {
			L"C:\\Program Files\\Tencent\\WeChat",
			L"C:\\Program Files (x86)\\Tencent\\WeChat",
			L"D:\\Program Files\\Tencent\\WeChat",
			L"E:\\Program Files\\Tencent\\WeChat",
			GetEnvVar(L"LOCALAPPDATA") + L"\\Programs\\Tencent\\WeChat",
			GetEnvVar(L"ProgramFiles") + L"\\Tencent\\WeChat",
			GetEnvVar(L"ProgramFiles(x86)") + L"\\Tencent\\WeChat"
		};

		for (const auto& path : paths) {
			// std::cout << ortToUtf8(L"检查路径: ") << ortToUtf8(path) << std::endl;
			if (fs::exists(path) && fs::is_directory(path)) {
				installPath = path;
				// std::cout << ortToUtf8(L"找到有效路径: ") << ortToUtf8(installPath) << std::endl;
				break;
			}
		}
	}

	if (installPath.empty()) {
		throw std::runtime_error(ortToUtf8(L"无法找到微信安装路径").c_str());
	}

	// 3. 验证主程序是否存在
	fs::path base = installPath;
	fs::path exePath = base / L"WeChat.exe";

	if (!fs::exists(exePath)) {
		exePath = base / L"Weixin.exe";
		// std::cout << ortToUtf8(L"WeChat.exe不存在，尝试Weixin.exe...") << std::endl;
	}

	if (!fs::exists(exePath)) {
		throw std::runtime_error(ortToUtf8(L"未找到微信主程序(WeChat.exe或Weixin.exe)").c_str());
	}

	// std::cout << ortToUtf8(L"找到的主程序路径: ") << ortToUtf8(exePath.wstring()) << std::endl;

	// 4. 获取版本信息
	DWORD handle = 0;
	DWORD verSize = GetFileVersionInfoSizeW(exePath.c_str(), &handle);
	if (verSize == 0) {
		DWORD err = GetLastError();
		std::string errMsg = ortToUtf8(L"GetFileVersionInfoSizeW 失败，错误代码: ") + std::to_string(err);
		throw std::runtime_error(errMsg.c_str());
	}

	std::vector<BYTE> verData(verSize);
	if (!GetFileVersionInfoW(exePath.c_str(), handle, verSize, verData.data())) {
		DWORD err = GetLastError();
		std::string errMsg = ortToUtf8(L"GetFileVersionInfoW 失败，错误代码: ") + std::to_string(err);
		throw std::runtime_error(errMsg.c_str());
	}

	VS_FIXEDFILEINFO* verInfo = nullptr;
	UINT sizeInfo = 0;
	if (!VerQueryValueW(verData.data(), L"\\", reinterpret_cast<LPVOID*>(&verInfo), &sizeInfo)) {
		throw std::runtime_error(ortToUtf8(L"VerQueryValueW 失败").c_str());
	}

	int major = HIWORD(verInfo->dwFileVersionMS);
	int minor = LOWORD(verInfo->dwFileVersionMS);
	int build = HIWORD(verInfo->dwFileVersionLS);
	int rev = LOWORD(verInfo->dwFileVersionLS);

	std::wstring versionString = std::to_wstring(major) + L"." +
		std::to_wstring(minor) + L"." +
		std::to_wstring(build) + L"." +
		std::to_wstring(rev);

	// std::cout << ortToUtf8(L"获取到的版本号: ") << ortToUtf8(versionString) << std::endl;

	// 5. 根据版本确定路径
	std::wstring appdata = GetEnvVar(L"APPDATA");
	fs::path wechatPath;
	fs::path wechatOcrPath;

	if (major == 4) {
		// std::cout << ortToUtf8(L"检测到微信版本4.x") << std::endl;
		wechatPath = base / versionString;
		wechatOcrPath = fs::path(appdata) / L"Tencent\\xwechat\\XPlugin\\plugins\\WeChatOcr";

		if (fs::exists(wechatOcrPath)) {
			// std::cout << ortToUtf8(L"开始遍历OCR插件目录...") << std::endl;
			for (const auto& entry : fs::directory_iterator(wechatOcrPath)) {
				if (entry.is_directory()) {
					wechatOcrPath = entry.path() / L"extracted\\wxocr.dll";
					if (fs::exists(wechatOcrPath)) {
						// std::cout << ortToUtf8(L"找到OCRDLL路径: ") << ortToUtf8(wechatOcrPath.wstring()) << std::endl;
						break;
					}
				}
			}
		}
	}
	else if (major == 3) {
		// std::cout << ortToUtf8(L"检测到微信版本3.x") << std::endl;
		wechatPath = base / (L"[" + versionString + L"]");
		wechatOcrPath = fs::path(appdata) / L"Tencent\\WeChat\\XPlugin\\Plugins\\WeChatOCR";

		if (fs::exists(wechatOcrPath)) {
			// std::cout << ortToUtf8(L"开始遍历OCR插件目录...") << std::endl;
			for (const auto& entry : fs::directory_iterator(wechatOcrPath)) {
				if (entry.is_directory()) {
					wechatOcrPath = entry.path() / L"extracted\\WeChatOCR.exe";
					if (fs::exists(wechatOcrPath)) {
						// std::cout << ortToUtf8(L"找到OCR可执行文件路径: ") << ortToUtf8(wechatOcrPath.wstring()) << std::endl;
						break;
					}
				}
			}
		}
	}

	if (wechatOcrPath.empty() || !fs::exists(wechatOcrPath)) {
		// std::cout << ortToUtf8(L"警告: 未找到OCR组件") << std::endl;
	}

	if (wechatPath.empty() || !fs::exists(wechatPath)) {
		// std::cout << ortToUtf8(L"警告: 未找到微信数据路径") << std::endl;
	}

	// std::cout << ortToUtf8(L"最终OCR路径: ") << ortToUtf8(wechatOcrPath.wstring()) << std::endl;
	// std::cout << ortToUtf8(L"最终微信路径: ") << ortToUtf8(wechatPath.wstring()) << std::endl;

	return { wechatOcrPath.wstring(), wechatPath.wstring() };
}
int main(int argc, char* argv[]) {
	SetConsoleOutputCP(CP_UTF8); 
	std::string img_path= "";
	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--img" && i + 1 < argc) {
			img_path = argv[++i];  
		}
	}
	if (img_path == "")
	{
		return 0;
	}
	auto exe_dir = getExeDirOrt();
	//std::wstring ocr_exe = utf8ToWstring(ortToUtf8(exe_dir) + "\\WeChat\\WeChatOCR.exe");
	//std::wstring wechat_dir = utf8ToWstring(ortToUtf8(exe_dir) + "\\WeChat");
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
