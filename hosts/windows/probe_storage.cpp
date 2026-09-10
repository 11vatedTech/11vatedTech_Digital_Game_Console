// probe_storage.cpp — storage device discovery (canon §14.1 storage classes).
// Geometry/class discovery only; latency/throughput scores arrive with the
// microbenchmark phase and remain 0 until measured (C5/C10).
#include "win_util.hpp"
#include "dc/capability.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <winioctl.h>
#include <cstdio>
#include <cstring>

namespace dcwin {

using namespace dc;

namespace {

std::string BusTypeFromStor(STORAGE_BUS_TYPE bus) {
    switch (bus) {
        case BusTypeNvme: return "NVMe";
        case BusTypeSata: return "SATA";
        case BusTypeSas: return "SAS";
        case BusTypeUsb: return "USB";
        case BusTypeSd: return "SD";
        case BusTypeMmc: return "MMC";
        case BusTypeVirtual: return "Virtual";
        case BusTypeScsi: return "SCSI";
        case BusTypeAta: return "ATA";
        case BusTypeAtapi: return "ATAPI";
        case BusType1394: return "1394";
        case BusTypeFibre: return "Fibre";
        case BusTypeRAID: return "RAID";
        default: return "Unknown";
    }
}

void ProbeVolume(const std::string& root, uint64_t& capacity, uint64_t& free_bytes) {
    ULARGE_INTEGER total{}, free_quads{};
    std::string root_slash = root + "\\";
    if (GetDiskFreeSpaceExA(root_slash.c_str(), nullptr, &total, &free_quads)) {
        capacity = total.QuadPart;
        free_bytes = free_quads.QuadPart;
    }
}

} // namespace

void ProbeStorage(std::vector<StorageDeviceInfo>& out) {
    DWORD drives = GetLogicalDrives();
    char root[4] = {'A', ':', '\\', '\0'};
    int index = 0;
    for (int bit = 0; bit < 26; ++bit) {
        if (!(drives & (1u << bit))) continue;
        root[0] = static_cast<char>('A' + bit);

        UINT type = GetDriveTypeA(root);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;

        StorageDeviceInfo info;
        info.id = "disk" + std::to_string(index++);
        info.capacity_bytes = 0;
        info.free_bytes = 0;
        ProbeVolume(root, info.capacity_bytes, info.free_bytes);

        // IOCTL_STORAGE_QUERY_PROPERTY on the volume handle reports bus type.
        std::string device_path = std::string("\\\\.\\") + root[0] + ":";
        HANDLE handle = CreateFileA(device_path.c_str(), 0,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            union {
                STORAGE_DEVICE_DESCRIPTOR sdd;
                BYTE raw[512];
            } buffer{};
            STORAGE_PROPERTY_QUERY query{};
            query.PropertyId = StorageDeviceProperty;
            query.QueryType = PropertyStandardQuery;
            DWORD returned = 0;
            if (DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                                &buffer, sizeof(buffer), &returned, nullptr) &&
                buffer.sdd.BusType > 0) {
                info.bus_type = BusTypeFromStor(static_cast<STORAGE_BUS_TYPE>(buffer.sdd.BusType));
                // Product/vendor strings are offsets into the same buffer.
                if (buffer.sdd.ProductIdOffset > 0 && buffer.sdd.ProductIdOffset < sizeof(buffer)) {
                    const char* pid = reinterpret_cast<const char*>(buffer.raw) + buffer.sdd.ProductIdOffset;
                    info.media_type = pid;
                }
            } else {
                info.bus_type = "Unknown";
            }
            CloseHandle(handle);
        } else {
            info.bus_type = "Unknown";
        }

        info.klass = ClassifyStorage(type, info.bus_type);
        // read_seq_mbps / latency percentiles / gpu_decompression stay 0/false:
        // unmeasured. Never claim a streaming class without the benchmark (C10).
        info.read_seq_mbps = 0.0;
        info.gpu_decompression = false;

        out.push_back(std::move(info));
    }
}

} // namespace dcwin
