// Out-of-process matte / vision host for ExtGenesisRemover.
// ORT must NOT load inside PIXERA (v39-v42 froze the preview). This exe owns ORT.
//
// IPC: Local\GenesisRemoverMatteIpc_v57 + Kick/Stop events (see MatteIpc.h).
// Log: %TEMP%\GenesisRemoverMatteHost.log

#include "MatteIpc.h"
#include "matting/BgRemover.hpp"
#include "matting/VisionEffect.hpp"
#include "platform/Paths.hpp"

#include <Windows.h>
#include <d3d11_1.h>
#include <dxgi.h>

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace {

std::ofstream g_log;

void logLine(const std::string& line) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    // Stamp every line so dump/health-check greps work on host log too.
    const std::string full = std::to_string(ms) + " [" +
                             std::string(genesis::matte_ipc::kBuildStamp) + "] " + line;
    if (g_log) {
        g_log << full << '\n';
        g_log.flush();
    }
}

LUID toLuid(std::uint64_t value) {
    LUID luid{};
    luid.LowPart = static_cast<DWORD>(value & 0xffffffffu);
    luid.HighPart = static_cast<LONG>(value >> 32);
    return luid;
}

// DML device_id matches IDXGIFactory::EnumAdapters order (not EnumAdapters1).
// Logs every adapter; returns index matching want_luid (or -1).
int findDmlDeviceId(std::uint64_t want_luid) {
    IDXGIFactory* factory = nullptr;
    if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory))) ||
        !factory) {
        logLine("CreateDXGIFactory failed for DML device enum");
        return -1;
    }
    int matched = -1;
    std::string matched_name;
    int nvidia_id = -1;
    std::string nvidia_name;
    std::uint64_t nvidia_luid = 0;
    IDXGIAdapter* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC desc{};
        adapter->GetDesc(&desc);
        const std::uint64_t luid =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(desc.AdapterLuid.HighPart)) << 32) |
            desc.AdapterLuid.LowPart;
        char name[128]{};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
        char hex[32]{};
        std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(luid));
        logLine(std::string("DXGI adapter[") + std::to_string(i) + "] luid_hex=" + hex +
                " luid_dec=" + std::to_string(luid) + " " + name);
        const std::string name_s = name;
        std::string lower = name_s;
        for (char& c : lower) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        if (lower.find("nvidia") != std::string::npos || lower.find("geforce") != std::string::npos ||
            lower.find("quadro") != std::string::npos || lower.find("rtx") != std::string::npos) {
            nvidia_id = static_cast<int>(i);
            nvidia_name = name_s;
            nvidia_luid = luid;
        }
        if (want_luid != 0 && luid == want_luid) {
            matched = static_cast<int>(i);
            matched_name = name_s;
        }
        adapter->Release();
        adapter = nullptr;
    }
    factory->Release();

    if (matched >= 0) {
        char hex[32]{};
        std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(want_luid));
        logLine(std::string("ACTIVE GPU (PIXERA Adapter LUID match): DML:") + std::to_string(matched) +
                " luid_hex=" + hex + " " + matched_name);
        std::string lower = matched_name;
        for (char& c : lower) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        const bool on_igpu = lower.find("amd") != std::string::npos ||
                             lower.find("radeon(tm) graphics") != std::string::npos ||
                             lower.find("intel") != std::string::npos;
        if (on_igpu && nvidia_id >= 0) {
            char nhex[32]{};
            std::snprintf(nhex, sizeof(nhex), "%016llx", static_cast<unsigned long long>(nvidia_luid));
            logLine(std::string("NOTE: Remover is on iGPU/APU, not NVIDIA. To use ") + nvidia_name +
                    ", set PIXERA preview + Remover Adapter LUID to " + nhex +
                    " (DML:" + std::to_string(nvidia_id) + ")");
        }
    } else {
        logLine("ACTIVE GPU: no LUID match yet (will try DML device_id cascade)");
        if (nvidia_id >= 0) {
            char nhex[32]{};
            std::snprintf(nhex, sizeof(nhex), "%016llx", static_cast<unsigned long long>(nvidia_luid));
            logLine(std::string("NVIDIA seen at DML:") + std::to_string(nvidia_id) +
                    " luid_hex=" + nhex + " " + nvidia_name);
        }
    }
    return matched;
}

IDXGIAdapter1* findAdapter(IDXGIFactory1& factory, LUID want) {
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory.EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        if (want.HighPart == 0 && want.LowPart == 0) {
            return adapter;
        }
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.AdapterLuid.HighPart == want.HighPart && desc.AdapterLuid.LowPart == want.LowPart) {
            return adapter;
        }
        adapter->Release();
        adapter = nullptr;
    }
    return nullptr;
}

std::size_t countNz(const std::uint8_t* rgba, std::size_t bytes) {
    std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < bytes; i += 4) {
        if (rgba[i] | rgba[i + 1] | rgba[i + 2]) {
            ++n;
        }
    }
    return n;
}

struct CaptureState {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Texture2D* opened = nullptr;
    ID3D11Texture2D* staging = nullptr;
    HANDLE opened_handle = nullptr;
    std::uint32_t w = 0;
    std::uint32_t h = 0;

    void releaseOpened() {
        if (staging) {
            staging->Release();
            staging = nullptr;
        }
        if (opened) {
            opened->Release();
            opened = nullptr;
        }
        opened_handle = nullptr;
        w = h = 0;
    }

    void releaseAll() {
        releaseOpened();
        if (context) {
            context->Release();
            context = nullptr;
        }
        if (device) {
            device->Release();
            device = nullptr;
        }
    }

    bool ensureDevice(std::uint64_t luid) {
        if (device && context) {
            return true;
        }
        IDXGIFactory1* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) ||
            !factory) {
            logLine("CreateDXGIFactory1 failed");
            return false;
        }
        IDXGIAdapter1* adapter = findAdapter(*factory, toLuid(luid));
        factory->Release();
        if (!adapter) {
            logLine("adapter not found");
            return false;
        }
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        const HRESULT hr =
            D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &level, 1,
                              D3D11_SDK_VERSION, &device, nullptr, &context);
        adapter->Release();
        if (FAILED(hr) || !device || !context) {
            logLine("D3D11CreateDevice failed");
            releaseAll();
            return false;
        }
        logLine("D3D device ready");
        return true;
    }

    bool openCapture(HANDLE handle, std::uint32_t width, std::uint32_t height) {
        if (!handle || width == 0 || height == 0) {
            return false;
        }
        if (opened_handle == handle && opened && staging && w == width && h == height) {
            return true;
        }
        releaseOpened();

        ID3D11Device1* device1 = nullptr;
        if (FAILED(device->QueryInterface(__uuidof(ID3D11Device1),
                                          reinterpret_cast<void**>(&device1))) ||
            !device1) {
            logLine("ID3D11Device1 QI failed");
            return false;
        }
        ID3D11Texture2D* tex = nullptr;
        const HRESULT open_hr =
            device1->OpenSharedResource1(handle, __uuidof(ID3D11Texture2D),
                                         reinterpret_cast<void**>(&tex));
        device1->Release();
        if (FAILED(open_hr) || !tex) {
            logLine("OpenSharedResource1 failed hr=" + std::to_string(static_cast<unsigned long>(open_hr)));
            return false;
        }
        D3D11_TEXTURE2D_DESC desc{};
        tex->GetDesc(&desc);
        D3D11_TEXTURE2D_DESC staging_desc = desc;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.BindFlags = 0;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging_desc.MiscFlags = 0;
        ID3D11Texture2D* st = nullptr;
        if (FAILED(device->CreateTexture2D(&staging_desc, nullptr, &st)) || !st) {
            tex->Release();
            logLine("staging CreateTexture2D failed");
            return false;
        }
        opened = tex;
        staging = st;
        opened_handle = handle;
        w = width;
        h = height;
        logLine("opened CAPTURE share " + std::to_string(width) + "x" + std::to_string(height));
        return true;
    }

    bool mapToRgba(std::vector<std::uint8_t>& out, double* out_map_ms) {
        if (!opened || !staging || !context) {
            return false;
        }
        const auto t0 = std::chrono::steady_clock::now();
        context->CopyResource(staging, opened);
        D3D11_QUERY_DESC qd{};
        qd.Query = D3D11_QUERY_EVENT;
        ID3D11Query* q = nullptr;
        if (SUCCEEDED(device->CreateQuery(&qd, &q)) && q) {
            context->End(q);
            // Soft poll - avoid tight GetData+Sleep(1) spinning under PIXERA output load.
            int spins = 0;
            while (context->GetData(q, nullptr, 0, 0) == S_FALSE) {
                ++spins;
                Sleep(spins < 8 ? 1 : 2);
            }
            q->Release();
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)) || !mapped.pData) {
            logLine("Map failed");
            return false;
        }
        out.resize(static_cast<std::size_t>(w) * h * 4u);
        auto* src = static_cast<const std::uint8_t*>(mapped.pData);
        for (std::uint32_t y = 0; y < h; ++y) {
            std::memcpy(out.data() + static_cast<std::size_t>(y) * w * 4u,
                        src + static_cast<std::size_t>(y) * mapped.RowPitch, w * 4u);
        }
        context->Unmap(staging, 0);
        if (out_map_ms) {
            *out_map_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0)
                              .count();
        }
        return true;
    }
};

} // namespace

int main() {
    // Belt-and-suspenders with DLL CREATE_NO_WINDOW: never leave a blank console up.
    FreeConsole();

    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const std::filesystem::path log_path =
        std::filesystem::path(temp) / "GenesisRemoverMatteHost.log";
    g_log.open(log_path, std::ios::app);

    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    const std::filesystem::path module_dir = std::filesystem::path(module).parent_path();
    const std::filesystem::path roots[] = {module_dir, module_dir / "assets"};
    genesis::setAssetSearchRoots(roots);

    logLine(std::string("start ") + genesis::matte_ipc::kBuildStamp + " " + module_dir.string());
    {
        std::error_code ec;
        const auto host_sz = std::filesystem::file_size(module_dir / "GenesisRemoverMatteHost.exe", ec);
        logLine(std::string("IDENTITY dir=") + module_dir.string() +
                " host_bytes=" + std::to_string(ec ? 0ull : static_cast<unsigned long long>(host_sz)) +
                " ipc_ver=" + std::to_string(genesis::matte_ipc::kVersion) +
                " capture_path=prefer_shm");
    }
    {
        const auto vision_dir = module_dir / "assets" / "vision";
        logLine(std::string("vision assets dir=") + vision_dir.string() +
                (std::filesystem::exists(vision_dir) ? " (exists)" : " (MISSING)"));
        const auto rvm = module_dir / "assets" / "matting" / "rvm_mobilenetv3_fp16.onnx";
        if (!std::filesystem::exists(rvm)) {
            logLine("FATAL: RVM model missing: " + rvm.string());
            return 3;
        }
        if (genesis::isGitLfsPointerFile(rvm)) {
            logLine("FATAL: RVM model is Git LFS pointer - run git lfs pull, rebuild, reinstall: " +
                    rvm.string());
            return 3;
        }
        std::error_code ec;
        const auto sz = std::filesystem::file_size(rvm, ec);
        logLine("RVM model OK bytes=" + std::to_string(ec ? 0ull : static_cast<unsigned long long>(sz)));
    }

    HANDLE mapping =
        OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, genesis::matte_ipc::kMappingName);
    if (!mapping) {
        mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                     static_cast<DWORD>(genesis::matte_ipc::kMappingBytes),
                                     genesis::matte_ipc::kMappingName);
    }
    if (!mapping) {
        logLine("Open/CreateFileMapping failed");
        return 1;
    }
    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, genesis::matte_ipc::kMappingBytes);
    if (!view) {
        logLine("MapViewOfFile failed");
        CloseHandle(mapping);
        return 1;
    }
    HANDLE kick =
        OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, genesis::matte_ipc::kKickEventName);
    HANDLE stop =
        OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, genesis::matte_ipc::kStopEventName);
    if (!kick) {
        kick = CreateEventA(nullptr, FALSE, FALSE, genesis::matte_ipc::kKickEventName);
    }
    if (!stop) {
        stop = CreateEventA(nullptr, TRUE, FALSE, genesis::matte_ipc::kStopEventName);
    }
    if (!kick || !stop) {
        logLine("Open/CreateEvent failed");
        return 1;
    }

    auto* hdr = static_cast<genesis::matte_ipc::Header*>(view);
    if (hdr->magic != genesis::matte_ipc::kMagic) {
        std::memset(hdr, 0, sizeof(*hdr));
        hdr->magic = genesis::matte_ipc::kMagic;
    }
    hdr->version = genesis::matte_ipc::kVersion;
    hdr->host_pid = GetCurrentProcessId();
    std::memset(hdr->host_stamp, 0, sizeof(hdr->host_stamp));
    std::memcpy(hdr->host_stamp, genesis::matte_ipc::kBuildStamp,
                std::min(sizeof(hdr->host_stamp) - 1, std::strlen(genesis::matte_ipc::kBuildStamp)));
    if (hdr->dll_stamp[0] != '\0' &&
        std::strncmp(hdr->dll_stamp, genesis::matte_ipc::kBuildStamp,
                     genesis::matte_ipc::kStampBytes) != 0) {
        hdr->stamp_mismatch = 1;
        logLine(std::string("STAMP_MISMATCH dll=") + hdr->dll_stamp +
                " host=" + genesis::matte_ipc::kBuildStamp +
                " - quit PIXERA and reinstall ExtGenesisRemover package");
    } else {
        hdr->stamp_mismatch = 0;
        if (hdr->dll_stamp[0] == '\0') {
            // DLL will fill dll_stamp on next openIpc; leave mismatch clear until then.
            logLine("dll_stamp empty at host start (DLL will publish)");
        }
    }

    // Wait briefly for DLL to publish Adapter LUID (set before CreateProcess, usually ready).
    for (int i = 0; i < 50 && hdr->adapter_luid == 0; ++i) {
        Sleep(10);
    }
    const int dml_id = findDmlDeviceId(hdr->adapter_luid);
    logLine("PIXERA adapter_luid=" + std::to_string(hdr->adapter_luid) +
            " -> preferred DML device_id=" + std::to_string(dml_id));

    genesis::BgRemover remover;
    genesis::BgRemoverSettings settings;
    settings.backend = genesis::BgRemoverBackend::Auto;
    settings.quality = genesis::BgRemoverQuality::Fast;
    settings.vision_effect = genesis::VisionEffect::PersonMatteRvm;
    settings.result_as = genesis::VisionResultAs::Alpha;
    // Out-of-process: DirectML GPU is the intended path (in-PIXERA ORT froze preview).
    settings.prefer_cpu_ep = false;
    settings.dml_device_id = dml_id;
    settings.adapter_luid = hdr->adapter_luid;
    settings.temporal_alpha = 0.12f;
    // 0 = use Fast/Balanced/Quality presets from hdr->quality (do not force Fast downsample).
    settings.rvm_downsample = 0.0f;
    remover.setSettings(settings);
    std::string status;
    logLine("BgRemover initialize begin (DirectML GPU preferred)");
    if (!remover.initialize(&status)) {
        logLine("BgRemover init failed: " + status);
        std::snprintf(hdr->status, sizeof(hdr->status), "%s", status.c_str());
        return 2;
    }
    const std::string ep = remover.executionProvider();
    logLine("BgRemover ready ep=" + ep + " " + status);
    if (ep.rfind("DML", 0) != 0 && !ep.empty()) {
        logLine("WARNING: running on CPU - check DirectML.dll + onnxruntime DirectML build next to host");
    }
    std::snprintf(hdr->status, sizeof(hdr->status), "%s", ep.empty() ? status.c_str() : ep.c_str());
    hdr->host_ready = 1;
    // Keep local settings in sync with resolved model path so later setSettings
    // (quality / vision IPC) does not look like a path clear -> ORT reload.
    settings = remover.settings();

    CaptureState capture;
    std::vector<std::uint8_t> scratch_in;
    std::vector<std::uint8_t> scratch_out;
    std::uint64_t last_kick = 0;
    std::uint64_t frames = 0;
    int applied_quality = -1;
    // Match startup defaults so the first kick does not tear down a warm RVM session.
    int applied_effect = static_cast<int>(settings.vision_effect);
    int applied_result_as = static_cast<int>(settings.result_as);
    // Normalized model key: "" means "effect default" (empty IPC or default asset path).
    std::string applied_model_key;

    auto trimModel = [](std::string model) {
        while (!model.empty() &&
               (model.back() == '\0' || model.back() == ' ' || model.back() == '\t' ||
                model.back() == '\r' || model.back() == '\n')) {
            model.pop_back();
        }
        std::size_t start = 0;
        while (start < model.size() &&
               (model[start] == ' ' || model[start] == '\t' || model[start] == '\r' ||
                model[start] == '\n')) {
            ++start;
        }
        if (start > 0) {
            model.erase(0, start);
        }
        return model;
    };

    // Collapse empty / default-asset paths to one key so UI sending the default RVM
    // path does not thrash against IPC "".
    auto modelKeyForEffect = [&trimModel](int effect, std::string model) {
        model = trimModel(std::move(model));
        if (model.empty()) {
            return std::string{};
        }
        const char* def = genesis::visionEffectDefaultModel(
            static_cast<genesis::VisionEffect>(effect));
        if (def && *def) {
            if (model == def) {
                return std::string{};
            }
            const std::string def_s(def);
            if (model.size() >= def_s.size() &&
                model.compare(model.size() - def_s.size(), def_s.size(), def_s) == 0) {
                // Absolute resolved path ending in the default relative asset.
                if (model.size() == def_s.size() || model[model.size() - def_s.size() - 1] == '/' ||
                    model[model.size() - def_s.size() - 1] == '\\') {
                    return std::string{};
                }
            }
            const auto slash = def_s.find_last_of("/\\");
            const std::string base =
                slash == std::string::npos ? def_s : def_s.substr(slash + 1);
            if (!base.empty() && model.size() >= base.size() &&
                model.compare(model.size() - base.size(), base.size(), base) == 0) {
                if (model.size() == base.size() || model[model.size() - base.size() - 1] == '/' ||
                    model[model.size() - base.size() - 1] == '\\') {
                    return std::string{};
                }
            }
        }
        return model;
    };

    // ALWAYS advance done_seq on failure. If we leave kick_seq != done_seq, the DLL
    // stays busy=1 forever and never kicks again (v45 regression: nz=0 -> no cutout).
    auto failKick = [&](const char* reason) {
        logLine(std::string("kick fail: ") + reason);
        hdr->result_ok = 0;
        hdr->result_nz = 0;
        std::snprintf(hdr->stage, sizeof(hdr->stage), "%s", reason);
        hdr->done_seq = last_kick;
    };

    auto applyQualityFromHdr = [&]() {
        const int q = static_cast<int>(std::min<std::uint32_t>(3u, hdr->quality));
        float ds = hdr->rvm_downsample;
        std::uint32_t max_side = hdr->max_infer_side;
        if (q < 3) {
            ds = 0.0f;
            max_side = 0;
        } else {
            if (ds < 0.05f) {
                ds = 0.20f;
            }
            if (ds > 1.0f) {
                ds = 1.0f;
            }
            if (max_side < 128u) {
                max_side = 384u;
            }
            if (max_side > 1280u) {
                max_side = 1280u;
            }
        }
        const int ds_milli = static_cast<int>(ds * 1000.0f + 0.5f);
        const int key = (q << 20) ^ (ds_milli << 8) ^ static_cast<int>(max_side);
        if (key == applied_quality) {
            return;
        }
        const bool had_quality = applied_quality >= 0;
        settings.quality =
            q >= 3 ? genesis::BgRemoverQuality::Balanced : static_cast<genesis::BgRemoverQuality>(q);
        settings.rvm_downsample = ds;
        settings.rvm_max_infer_side = max_side;
        remover.setSettings(settings);
        // First quality publish matches startup defaults - do not wipe RVM recurrent state.
        if (had_quality) {
            remover.resetTemporalState();
        }
        applied_quality = key;
        settings = remover.settings();
        if (q >= 3) {
            logLine("quality -> Custom downsample=" + std::to_string(ds) +
                    " max_side=" + std::to_string(max_side));
        } else {
            logLine(std::string("quality -> ") + genesis::bgRemoverQualityLabel(settings.quality));
        }
    };

    auto applyVisionFromHdr = [&]() {
        const int effect = static_cast<int>(std::min<std::uint32_t>(9u, hdr->vision_effect));
        const int result = static_cast<int>(std::min<std::uint32_t>(2u, hdr->result_as));
        const std::string model = trimModel(std::string(hdr->model_path));
        const std::string model_key = modelKeyForEffect(effect, model);
        if (effect == applied_effect && result == applied_result_as &&
            model_key == applied_model_key) {
            return;
        }
        const bool session_change =
            (effect != applied_effect) || (model_key != applied_model_key);
        settings.vision_effect = static_cast<genesis::VisionEffect>(effect);
        settings.result_as = static_cast<genesis::VisionResultAs>(result);
        if (!model_key.empty()) {
            settings.model_path = model;
        } else if (session_change) {
            // Effect changed / default key - resolve the effect's default on init.
            settings.model_path.clear();
        }
        // else keep resolved path (Person Matte default "" must not unload RVM).

        if (session_change) {
            // RVM ignores activation/norm/size; skipping avoids Auto->None reload marks.
            if (settings.vision_effect != genesis::VisionEffect::PersonMatteRvm) {
                settings.activation =
                    genesis::visionEffectDefaultActivation(settings.vision_effect);
                settings.norm = genesis::visionEffectDefaultNorm(settings.vision_effect);
                settings.size_override =
                    genesis::visionEffectDefaultSize(settings.vision_effect);
            }
            remover.setSettings(settings);
            std::string init_status;
            // initialize() is idempotent when setSettings kept the session warm.
            const bool was_init = remover.isInitialized();
            (void)remover.initialize(&init_status);
            settings = remover.settings();
            if (was_init && remover.isInitialized()) {
                logLine(std::string("vision keep-warm ") +
                        genesis::visionEffectLabel(settings.vision_effect) +
                        " result=" + genesis::visionResultAsLabel(settings.result_as) +
                        " (no ORT reload)");
            } else {
                logLine(std::string("vision -> ") +
                        genesis::visionEffectLabel(settings.vision_effect) +
                        " result=" + genesis::visionResultAsLabel(settings.result_as) + " " +
                        init_status);
            }
            const std::string ep_now = remover.executionProvider();
            std::snprintf(hdr->status, sizeof(hdr->status), "%s",
                          ep_now.empty() ? init_status.c_str() : ep_now.c_str());
        } else {
            // Result As only - no ORT reload.
            remover.setSettings(settings);
            settings = remover.settings();
            logLine(std::string("result_as -> ") +
                    genesis::visionResultAsLabel(settings.result_as));
        }
        applied_effect = effect;
        applied_result_as = result;
        applied_model_key = model_key;
    };

    // Cycle timing lives at loop scope (not inside the legacy GPU branch).
    auto s_last_ok = std::chrono::steady_clock::now();

    for (;;) {
        HANDLE waits[] = {stop, kick};
        const DWORD wr = WaitForMultipleObjects(2, waits, FALSE, 50);
        if (wr == WAIT_OBJECT_0) {
            logLine("stop event");
            break;
        }

        if (hdr->kick_seq == last_kick) {
            continue;
        }
        last_kick = hdr->kick_seq;
        const std::uint32_t w = hdr->width;
        const std::uint32_t h = hdr->height;
        const HANDLE cap_handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(hdr->capture_handle));
        const bool capture_in_shm = hdr->capture_in_shm != 0;
        ++frames;
        if (frames <= 3u || (frames % 30u) == 1u) {
            logLine("kick seq=" + std::to_string(last_kick) + " " + std::to_string(w) + "x" +
                    std::to_string(h) + (capture_in_shm ? " shm" : " gpu"));
        }

        if (w == 0 || h == 0 || w > genesis::matte_ipc::kMaxWidth ||
            h > genesis::matte_ipc::kMaxHeight || (!capture_in_shm && !cap_handle)) {
            failKick("bad_params");
            continue;
        }

        applyQualityFromHdr();
        applyVisionFromHdr();
        if (hdr->dll_stamp[0] != '\0') {
            const bool bad =
                std::strncmp(hdr->dll_stamp, genesis::matte_ipc::kBuildStamp,
                             genesis::matte_ipc::kStampBytes) != 0;
            hdr->stamp_mismatch = bad ? 1u : 0u;
            if (bad && (frames % 60u) == 1u) {
                logLine(std::string("STAMP_MISMATCH dll=") + hdr->dll_stamp +
                        " host=" + genesis::matte_ipc::kBuildStamp);
            }
        }

        double map_ms = 0.0;
        if (capture_in_shm) {
            // v62: DLL already copied capture RGBA into IPC pixels - do NOT OpenShared/Map.
            // Mapping a shared capture on this process was stalling PIXERA presents.
            const std::size_t bytes = static_cast<std::size_t>(w) * h * 4u;
            scratch_in.resize(bytes);
            std::memcpy(scratch_in.data(), genesis::matte_ipc::pixels(hdr), bytes);
            map_ms = hdr->last_map_ms; // DLL-reported async download cost
            if ((frames % 30u) == 1u) {
                logLine("capture_from_shm bytes=" + std::to_string(bytes) +
                        " download_ms=" + std::to_string(map_ms));
            }
        } else {
            if (!capture.ensureDevice(hdr->adapter_luid) ||
                !capture.openCapture(cap_handle, w, h)) {
                failKick("open_capture");
                continue;
            }

            // Legacy GPU-share path (kept for old DLLs). Prefer capture_in_shm.
            static bool s_had_good = false;
            static double s_last_map_ms = 0.0;
            static int s_map_skip_stride = 0;
            if (!s_had_good) {
                Sleep(40);
            }

            bool mapped = false;
            const bool allow_skip_map =
                s_had_good && !scratch_in.empty() &&
                scratch_in.size() == static_cast<std::size_t>(w) * h * 4u &&
                s_last_map_ms >= 28.0 && (s_map_skip_stride++ % 2) == 1;
            if (allow_skip_map) {
                map_ms = s_last_map_ms;
                mapped = true;
            } else if (!capture.mapToRgba(scratch_in, &map_ms)) {
                failKick("map_failed");
                continue;
            } else {
                mapped = true;
                s_last_map_ms = map_ms;
            }

            std::size_t nz_legacy = countNz(scratch_in.data(), scratch_in.size());
            if (nz_legacy == 0 && !allow_skip_map) {
                Sleep(50);
                if (!capture.mapToRgba(scratch_in, &map_ms)) {
                    failKick("map_retry_failed");
                    continue;
                }
                s_last_map_ms = map_ms;
                nz_legacy = countNz(scratch_in.data(), scratch_in.size());
            }
            if (nz_legacy == 0) {
                failKick("capture_nz0");
                continue;
            }
            s_had_good = true;
            (void)mapped;
        }

        const std::size_t nz = countNz(scratch_in.data(), scratch_in.size());
        if (nz == 0) {
            failKick("capture_nz0");
            continue;
        }

    scratch_out.resize(scratch_in.size());
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = remover.process(scratch_in.data(), scratch_out.data(), w, h);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (!ok) {
        failKick("rvm_failed");
        continue;
    }

    const std::size_t out_nz = countNz(scratch_out.data(), scratch_out.size());
    std::memcpy(genesis::matte_ipc::pixels(hdr), scratch_out.data(), scratch_out.size());
    hdr->result_nz = static_cast<std::uint32_t>(out_nz);
    hdr->result_ok = 1;
    std::snprintf(hdr->stage, sizeof(hdr->stage), "matte");
    hdr->done_seq = last_kick;
    const auto now = std::chrono::steady_clock::now();
    const double cycle_ms =
        std::chrono::duration<double, std::milli>(now - s_last_ok).count();
    s_last_ok = now;
    hdr->last_cycle_ms = static_cast<float>(cycle_ms);
    hdr->last_infer_ms = static_cast<float>(ms);
    hdr->last_map_ms = static_cast<float>(map_ms);
    if (frames <= 3u || (frames % 30u) == 1u) {
        const double cycle_fps = cycle_ms > 1.0 ? (1000.0 / cycle_ms) : 0.0;
        std::string qlab;
        if (static_cast<int>(hdr->quality) >= 3) {
            qlab = "Custom(ds=" + std::to_string(settings.rvm_downsample) +
                   ",side=" + std::to_string(settings.rvm_max_infer_side) + ")";
        } else {
            qlab = genesis::bgRemoverQualityLabel(settings.quality);
        }
        logLine("matte_ok nz=" + std::to_string(out_nz) + " infer_ms=" + std::to_string(ms) +
                " map_ms=" + std::to_string(map_ms) + " ep=" + remover.executionProvider() +
                " in_nz=" + std::to_string(nz) + " size=" + std::to_string(w) + "x" +
                std::to_string(h) + " q=" + qlab +
                " effect=" + genesis::visionEffectLabel(settings.vision_effect) +
                " cycle_ms=" + std::to_string(cycle_ms) +
                " matte_fps=" + std::to_string(cycle_fps));
        logLine("HEALTH stamp=" + std::string(genesis::matte_ipc::kBuildStamp) +
                " shm=" + std::to_string(capture_in_shm ? 1 : 0) +
                " map_ms=" + std::to_string(map_ms) +
                " infer_ms=" + std::to_string(ms) +
                " cycle_ms=" + std::to_string(cycle_ms) +
                " ep=" + remover.executionProvider());
    }
    }

    capture.releaseAll();
    hdr->host_ready = 0;
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    CloseHandle(kick);
    CloseHandle(stop);
    logLine("exit");
    return 0;
}
