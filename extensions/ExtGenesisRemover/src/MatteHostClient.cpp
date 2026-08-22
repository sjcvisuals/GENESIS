#include "WinPrefix.h"
#include "MatteHostClient.h"

#include "D3d11SharedTexture.h"
#include "ModulePaths.h"
#include "RemoverLog.h"

#include <Windows.h>

#include <cstring>
#include <filesystem>
#include <format>
#include <string>

namespace rxext::genesis_remover {
namespace {

::genesis::matte_ipc::Header* asHeader(void* view) {
    return static_cast<::genesis::matte_ipc::Header*>(view);
}

} // namespace

MatteHostClient::MatteHostClient() = default;

MatteHostClient::~MatteHostClient() {
    if (stop_event_) {
        SetEvent(static_cast<HANDLE>(stop_event_));
    }
    if (host_process_) {
        WaitForSingleObject(static_cast<HANDLE>(host_process_), 2000);
        CloseHandle(static_cast<HANDLE>(host_process_));
        host_process_ = nullptr;
    }
    closeIpc();
}

void MatteHostClient::closeIpc() {
    if (view_) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (mapping_) {
        CloseHandle(static_cast<HANDLE>(mapping_));
        mapping_ = nullptr;
    }
    if (kick_event_) {
        CloseHandle(static_cast<HANDLE>(kick_event_));
        kick_event_ = nullptr;
    }
    if (stop_event_) {
        CloseHandle(static_cast<HANDLE>(stop_event_));
        stop_event_ = nullptr;
    }
    started_ = false;
}

bool MatteHostClient::openIpc() {
    if (view_) {
        return true;
    }
    HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                        static_cast<DWORD>(::genesis::matte_ipc::kMappingBytes),
                                        ::genesis::matte_ipc::kMappingName);
    if (!mapping) {
        removerLog(std::format("MatteHostClient CreateFileMapping failed err={}", GetLastError()));
        return false;
    }
    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                               ::genesis::matte_ipc::kMappingBytes);
    if (!view) {
        CloseHandle(mapping);
        removerLog(std::format("MatteHostClient MapViewOfFile failed err={}", GetLastError()));
        return false;
    }
    HANDLE kick = CreateEventA(nullptr, FALSE, FALSE, ::genesis::matte_ipc::kKickEventName);
    HANDLE stop = CreateEventA(nullptr, TRUE, FALSE, ::genesis::matte_ipc::kStopEventName);
    if (!kick || !stop) {
        if (kick) {
            CloseHandle(kick);
        }
        if (stop) {
            CloseHandle(stop);
        }
        UnmapViewOfFile(view);
        CloseHandle(mapping);
        removerLog("MatteHostClient CreateEvent failed");
        return false;
    }

    mapping_ = mapping;
    view_ = view;
    kick_event_ = kick;
    stop_event_ = stop;

    auto* h = asHeader(view_);
    if (h->magic != ::genesis::matte_ipc::kMagic) {
        std::memset(h, 0, sizeof(*h));
        h->magic = ::genesis::matte_ipc::kMagic;
    }
    h->version = ::genesis::matte_ipc::kVersion;
    h->dll_pid = GetCurrentProcessId();
    h->adapter_luid = adapter_luid_;
    writeQualityToHeader();
    ResetEvent(static_cast<HANDLE>(stop_event_));
    return true;
}

bool MatteHostClient::launchHost() {
    if (host_process_) {
        DWORD code = STILL_ACTIVE;
        if (GetExitCodeProcess(static_cast<HANDLE>(host_process_), &code) && code == STILL_ACTIVE) {
            return true;
        }
        CloseHandle(static_cast<HANDLE>(host_process_));
        host_process_ = nullptr;
    }

    const std::filesystem::path dir = extensionModuleDirectory();
    const std::filesystem::path exe = dir / "GenesisRemoverMatteHost.exe";
    if (!std::filesystem::exists(exe)) {
        removerLog(std::format("MatteHost missing: {}", exe.string()));
        return false;
    }

    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    // CREATE_NO_WINDOW: no blank console flash for the matte host.
    constexpr DWORD kFlags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
    if (!CreateProcessW(exe.wstring().c_str(), cmd.data(), nullptr, nullptr, FALSE, kFlags, nullptr,
                        dir.wstring().c_str(), &si, &pi)) {
        removerLog(std::format("MatteHost CreateProcess failed err={}", GetLastError()));
        return false;
    }
    CloseHandle(pi.hThread);
    host_process_ = pi.hProcess;
    removerLog(std::format("MatteHost launched pid={}", pi.dwProcessId));
    return true;
}

bool MatteHostClient::ensureStarted(std::uint64_t adapter_luid) {
    adapter_luid_ = adapter_luid;
    if (!openIpc()) {
        return false;
    }
    asHeader(view_)->adapter_luid = adapter_luid_;
    if (!launchHost()) {
        return false;
    }
    started_ = true;
    return true;
}

bool MatteHostClient::hostReady() const {
    if (!view_) {
        return false;
    }
    const auto* h = asHeader(view_);
    return h->host_ready != 0 && h->host_pid != 0;
}

void MatteHostClient::writeQualityToHeader() {
    if (!view_) {
        return;
    }
    auto* h = asHeader(view_);
    h->quality = static_cast<std::uint32_t>(quality_);
    h->rvm_downsample = custom_downsample_;
    h->max_infer_side = custom_max_infer_side_;
}

void MatteHostClient::setQuality(int quality_0_to_3, float custom_downsample,
                                 std::uint32_t custom_max_infer_side) {
    quality_ = quality_0_to_3 < 0 ? 0 : (quality_0_to_3 > 3 ? 3 : quality_0_to_3);
    custom_downsample_ = custom_downsample;
    custom_max_infer_side_ = custom_max_infer_side;
    writeQualityToHeader();
}

bool MatteHostClient::busy() const {
    if (!view_) {
        return false;
    }
    const auto* h = asHeader(view_);
    return h->kick_seq != h->done_seq;
}

std::string MatteHostClient::hostEp() const {
    if (!view_) {
        return {};
    }
    return std::string(asHeader(view_)->status);
}

void MatteHostClient::clearBusyIfStale(std::uint32_t stale_ms) {
    if (!view_ || !busy() || last_kick_tick_ms_ == 0) {
        return;
    }
    const std::uint32_t now = GetTickCount();
    if (now - last_kick_tick_ms_ < stale_ms) {
        return;
    }
    auto* h = asHeader(view_);
    removerLog(std::format("MatteHostClient stale busy kick={} done={} — acking locally",
                           h->kick_seq, h->done_seq));
    h->result_ok = 0;
    h->done_seq = h->kick_seq;
    last_kick_tick_ms_ = 0;
}

bool MatteHostClient::duplicateCaptureHandle(void* local_nt_handle) {
    if (!local_nt_handle || !host_process_ || !view_) {
        return false;
    }
    const auto local_u = reinterpret_cast<std::uint64_t>(local_nt_handle);
    if (local_u == last_dup_handle_ && asHeader(view_)->capture_handle != 0) {
        return true;
    }

    HANDLE remote = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), static_cast<HANDLE>(local_nt_handle),
                         static_cast<HANDLE>(host_process_), &remote, 0, FALSE,
                         DUPLICATE_SAME_ACCESS)) {
        removerLog(std::format("MatteHostClient DuplicateHandle failed err={}", GetLastError()));
        return false;
    }
    asHeader(view_)->capture_handle = reinterpret_cast<std::uint64_t>(remote);
    last_dup_handle_ = local_u;
    removerLog(std::format("MatteHostClient duplicated capture handle to host"));
    return true;
}

bool MatteHostClient::kickCapture(D3d11SharedTexture& capture, std::uint32_t width,
                                  std::uint32_t height) {
    if (!ensureStarted(adapter_luid_) || !view_ || !hostReady()) {
        return false;
    }
    if (width == 0 || height == 0 || width > ::genesis::matte_ipc::kMaxWidth ||
        height > ::genesis::matte_ipc::kMaxHeight) {
        removerLog(std::format("MatteHostClient kick rejected size {}x{}", width, height));
        return false;
    }
    if (busy()) {
        return false;
    }
    if (!capture.shareHandle()) {
        return false;
    }
    if (!duplicateCaptureHandle(capture.shareHandle())) {
        return false;
    }

    auto* h = asHeader(view_);
    h->width = width;
    h->height = height;
    h->adapter_luid = adapter_luid_;
    writeQualityToHeader();
    h->result_ok = 0;
    ++h->kick_seq;
    SetEvent(static_cast<HANDLE>(kick_event_));
    last_kick_tick_ms_ = GetTickCount();
    // Throttle: per-kick log + flush tanks matte FPS on disk.
    if ((h->kick_seq % 30u) == 1u) {
        removerLog(std::format("MatteHostClient kick seq={} {}x{}", h->kick_seq, width, height));
    }
    return true;
}

bool MatteHostClient::tryTakeMatte(std::vector<std::uint8_t>& out_rgba, std::uint32_t& out_w,
                                   std::uint32_t& out_h, ::genesis::RemoverFrameDebug* dbg) {
    if (!view_) {
        return false;
    }
    auto* h = asHeader(view_);
    if (h->done_seq == 0 || h->done_seq == last_done_seq_ || h->result_ok == 0) {
        return false;
    }
    if (h->width == 0 || h->height == 0 ||
        h->width > ::genesis::matte_ipc::kMaxWidth || h->height > ::genesis::matte_ipc::kMaxHeight) {
        return false;
    }
    const std::size_t bytes = static_cast<std::size_t>(h->width) * h->height * 4u;
    out_rgba.resize(bytes);
    std::memcpy(out_rgba.data(), ::genesis::matte_ipc::pixels(h), bytes);
    out_w = h->width;
    out_h = h->height;
    last_done_seq_ = h->done_seq;
    if (dbg) {
        dbg->width = out_w;
        dbg->height = out_h;
        dbg->has_video_in = true;
        dbg->download_ok = true;
        dbg->matte_copied = true;
        dbg->nonzero_rgb = h->result_nz;
        dbg->stage = "matte";
    }
    if ((h->done_seq % 30u) == 1u) {
        removerLog(std::format("MatteHostClient took matte seq={} nz={} stage={}", h->done_seq,
                               h->result_nz, h->stage));
    }
    return true;
}

} // namespace rxext::genesis_remover
