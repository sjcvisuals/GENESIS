#include "WinPrefix.h"
#include "AdapterGpu.h"

#include "RemoverLog.h"

#include <dxgi.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#pragma comment(lib, "dxgi.lib")

namespace rxext::genesis_remover {
namespace {

std::string toUtf8(const WCHAR* wide) {
    char name[256]{};
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, name, sizeof(name), nullptr, nullptr);
    return name;
}

std::string toLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool isSoftware(const DXGI_ADAPTER_DESC1& desc) {
    return (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
}

int countDesktopOutputs(IDXGIAdapter1& adapter) {
    int n = 0;
    IDXGIOutput* output = nullptr;
    for (UINT i = 0; adapter.EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND; ++i) {
        if (output) {
            DXGI_OUTPUT_DESC od{};
            if (SUCCEEDED(output->GetDesc(&od)) && od.AttachedToDesktop) {
                ++n;
            }
            output->Release();
            output = nullptr;
        }
    }
    return n;
}

std::uint64_t luidValue(const LUID& l) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(l.HighPart)) << 32) | l.LowPart;
}

int scoreAdapter(const DXGI_ADAPTER_DESC1& desc, int desktop_outputs) {
    const std::string lower = toLower(toUtf8(desc.Description));
    int score = 0;
    if (desktop_outputs > 0) {
        score += 100000 + desktop_outputs * 1000;
    }
    if (lower.find("nvidia") != std::string::npos || lower.find("geforce") != std::string::npos ||
        lower.find("quadro") != std::string::npos || lower.find("rtx") != std::string::npos) {
        score += 50000;
    }
    // AMD discrete usually has "RX" / "Radeon Pro" / "Radeon RX"; iGPU is often bare "Radeon(TM) Graphics".
    const bool amd_igpu = lower.find("amd") != std::string::npos &&
                          (lower.find("radeon(tm) graphics") != std::string::npos ||
                           lower.find("radeon graphics") != std::string::npos) &&
                          lower.find(" rx") == std::string::npos;
    if (amd_igpu || lower.find("intel") != std::string::npos) {
        score -= 20000;
    }
    // Prefer more VRAM as a weak tie-break (MB).
    score += static_cast<int>(desc.DedicatedVideoMemory / (1024u * 1024u));
    return score;
}

} // namespace

std::uint64_t pickAdapterLuidWithDisplays() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) ||
        !factory) {
        removerLog("pickAdapter: CreateDXGIFactory1 failed");
        return 0;
    }

    struct Cand {
        std::uint64_t luid = 0;
        int score = 0;
        int outputs = 0;
        std::string name;
        SIZE_T vram = 0;
    };
    std::vector<Cand> cands;

    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (isSoftware(desc)) {
            adapter->Release();
            adapter = nullptr;
            continue;
        }
        const int outs = countDesktopOutputs(*adapter);
        Cand c;
        c.luid = luidValue(desc.AdapterLuid);
        c.outputs = outs;
        c.name = toUtf8(desc.Description);
        c.vram = desc.DedicatedVideoMemory;
        c.score = scoreAdapter(desc, outs);
        removerLog(std::format("pickAdapter candidate[{}] outs={} score={} luid={:016x} {}", i, outs,
                               c.score, c.luid, c.name));
        cands.push_back(std::move(c));
        adapter->Release();
        adapter = nullptr;
    }
    factory->Release();

    if (cands.empty()) {
        return 0;
    }
    const auto best = std::max_element(cands.begin(), cands.end(),
                                       [](const Cand& a, const Cand& b) { return a.score < b.score; });
    removerLog(std::format("pickAdapter AUTO luid={:016x} outs={} {}", best->luid, best->outputs,
                           best->name));
    return best->luid;
}

std::string adapterDescription(std::uint64_t luid) {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) ||
        !factory) {
        return {};
    }
    std::string name;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (luidValue(desc.AdapterLuid) == luid) {
            name = toUtf8(desc.Description);
            adapter->Release();
            break;
        }
        adapter->Release();
        adapter = nullptr;
    }
    factory->Release();
    return name;
}

} // namespace rxext::genesis_remover
