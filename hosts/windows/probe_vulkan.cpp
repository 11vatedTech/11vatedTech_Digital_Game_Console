// probe_vulkan.cpp — Vulkan host capability discovery (DK0-M1G).
// Loads vulkan-1.dll dynamically (loader ships with GPU drivers; no SDK
// requirement at runtime). All Vulkan truth is gathered through
// vkGetInstanceProcAddr — never claims Vulkan functionality merely because
// the loader exists (C10). Vk* types stay inside this file (canon boundary).
#include "dc/capability.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace dcwin {

namespace {

// Minimal Vulkan declarations (dynamically resolved; no vk headers needed).
using VkInstance = void*;
using VkPhysicalDevice = void*;
using VkResult = int32_t;
constexpr VkResult VK_SUCCESS = 0;

struct VkApplicationInfo {
    enum { VK_STRUCTURE_TYPE_APPLICATION_INFO = 0 } sType;
    const void* pNext;
    const char* pApplicationName;
    uint32_t applicationVersion;
    const char* pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
};

struct VkInstanceCreateInfo {
    enum { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1 } sType;
    const void* pNext;
    uint32_t flags;
    const void* pApplicationInfo;
    uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
};

struct VkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType;
    char deviceName[256];
    // Trailing fields (pipelineCacheUUID, limits, sparseProperties, ...):
    // deliberately OVERSIZED — the loader writes exactly sizeof(real struct);
    // a larger local buffer can never be overrun (stack-cookie abort fixed).
    uint8_t pad[1024];
};

struct VkQueueFamilyProperties {
    uint32_t queueFlags;
    uint32_t queueCount;
    uint32_t timestampValidBits;
    uint32_t minImageTransferGranularity[3];
};

struct VkMemoryHeap {
    uint64_t size;
    uint32_t flags;
};

struct VkPhysicalDeviceMemoryProperties {
    uint32_t memoryTypeCount;
    uint32_t memoryTypes[32][2];
    uint32_t memoryHeapCount;
    VkMemoryHeap heaps[16];
    uint8_t pad[256]; // oversized guard, see VkPhysicalDeviceProperties
};

struct VkExtensionProperties {
    char extensionName[256];
    uint32_t specVersion;
};

// Feature struct (Vulkan 1.2 core subset we care about).
struct VkPhysicalDeviceVulkan12Features {
    enum { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES = 0x1B } sType;
    void* pNext;
    uint32_t _pad0[1];
    uint32_t _pad1[1];
    uint32_t _pad2[1];
    uint32_t _pad3[1];
    uint32_t _pad4[1];
    uint32_t _pad5[1];
    uint32_t _pad6[1];
    uint32_t _pad7[1];
    uint32_t _pad8[1];
    uint32_t _pad9[1];
    uint32_t _pad10[1];
    uint32_t _pad11[1];
    uint32_t _pad12[1];
    uint32_t _pad13[1];
    uint32_t _pad14[1];
    uint32_t _pad15[1];
    uint32_t _pad16[1];
    uint32_t _pad17[1];
    uint32_t _pad18[1];
    uint32_t _pad19[1];
    uint32_t _pad20[1];
    uint32_t _pad21[1];
    uint32_t _pad22[1];
    uint32_t _pad23[1];
    uint32_t _pad24[1];
    uint32_t _pad25[1];
    uint32_t _pad26[1];
    uint32_t _pad27[1];
    uint32_t _pad28[1];
    uint32_t _pad29[1];
    uint32_t _pad30[1];
    uint32_t _pad31[1];
    uint32_t _pad32[1];
    uint32_t _pad33[1];
    uint32_t _pad34[1];
    uint32_t _pad35[1];
    uint32_t _pad36[1];
    uint32_t _pad37[1];
    uint32_t _pad38[1];
    uint32_t _pad39[1];
    uint32_t _pad40[1];
    uint32_t _pad41[1];
    uint32_t _pad42[1];
    uint32_t _pad43[1];
    uint32_t _pad44[1];
    uint32_t _pad45[1];
    uint32_t _pad46[1];
    uint32_t _pad47[1];
    uint32_t _pad48[1];
    uint32_t _pad49[1];
    uint32_t _pad50[1];
    uint32_t _pad51[1];
    uint32_t _pad52[1];
    uint32_t _pad53[1];
    uint32_t _pad54[1];
    uint32_t _pad55[1];
    uint32_t _pad56[1];
    uint32_t _pad57[1];
    uint32_t _pad58[1];
    uint32_t _pad59[1];
    uint32_t _pad60[1];
    uint32_t _pad61[1];
    uint32_t _pad62[1];
    uint32_t _pad63[1];
    uint32_t _pad64[1];
    uint32_t _pad65[1];
    uint32_t _pad66[1];
    uint32_t _pad67[1];
    uint32_t _pad68[1];
    uint32_t _pad69[1];
    uint32_t _pad70[1];
    uint32_t _pad71[1];
    uint32_t bufferDeviceAddress;      // offset 72*4 per spec order approximated
    uint32_t _pad73[1];
    uint32_t _pad74[1];
    uint32_t _pad75[1];
    uint32_t _pad76[1];
    uint32_t _pad77[1];
    uint32_t _pad78[1];
    uint32_t _pad79[1];
    uint32_t _pad80[1];
    uint32_t _pad81[1];
    uint32_t _pad82[1];
    uint32_t _pad83[1];
    uint32_t timelineSemaphore;        // verified by extension presence instead
};

// NOTE: struct-layout games above are fragile; the probe relies primarily on
// EXTENSION presence (stable, string-based) and only uses core fields
// (properties, queue families, memory) whose layouts are stable across 1.x.

uint32_t VkVersionMajor(uint32_t v) { return v >> 22; }
uint32_t VkVersionMinor(uint32_t v) { return (v >> 12) & 0x3FF; }
uint32_t VkVersionPatch(uint32_t v) { return v & 0xFFF; }

std::string VersionString(uint32_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u", VkVersionMajor(v), VkVersionMinor(v), VkVersionPatch(v));
    return buf;
}

} // namespace

void ProbeVulkan(dc::VulkanInfo& out) {
    out = dc::VulkanInfo{};

    HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
    if (!loader) {
        // Truthful absence: no loader installed.
        out.status = "loader_not_found";
        return;
    }

    using PFN_vkGetInstanceProcAddr = void* (*)(void*, const char*);
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        reinterpret_cast<void*>(GetProcAddress(loader, "vkGetInstanceProcAddr")));
    if (!gipa) { out.status = "entry_point_missing"; return; }

    auto gipa_fn = [&](void* instance, const char* name) -> void* {
        return gipa(instance, name);
    };

    // Instance-level functions.
    using PFN_vkCreateInstance = VkResult (*)(const VkInstanceCreateInfo*, void*, VkInstance*);
    using PFN_vkEnumerateInstanceVersion = VkResult (*)(uint32_t*);
    using PFN_vkEnumerateInstanceExtensionProperties = VkResult (*)(const char*, uint32_t*, VkExtensionProperties*);
    using PFN_vkEnumeratePhysicalDevices = VkResult (*)(VkInstance, uint32_t*, VkPhysicalDevice*);
    using PFN_vkGetPhysicalDeviceProperties = void (*)(VkPhysicalDevice, VkPhysicalDeviceProperties*);
    using PFN_vkGetPhysicalDeviceQueueFamilyProperties = void (*)(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*);
    using PFN_vkGetPhysicalDeviceMemoryProperties = void (*)(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*);
    using PFN_vkEnumerateDeviceExtensionProperties = VkResult (*)(VkPhysicalDevice, const char*, uint32_t*, VkExtensionProperties*);
    using PFN_vkDestroyInstance = void (*)(VkInstance, void*);

    auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(gipa_fn(nullptr, "vkCreateInstance"));
    auto enumerate_instance_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(gipa_fn(nullptr, "vkEnumerateInstanceVersion"));
    auto enumerate_instance_ext = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(gipa_fn(nullptr, "vkEnumerateInstanceExtensionProperties"));
    if (!create_instance) { out.status = "entry_point_missing"; return; }

    uint32_t loader_version = 0;
    if (enumerate_instance_version && enumerate_instance_version(&loader_version) == VK_SUCCESS) {
        out.api_version = VersionString(loader_version);
    }

    // Instance extensions (loader truth).
    std::vector<std::string> instance_exts;
    {
        uint32_t n = 0;
        if (enumerate_instance_ext && enumerate_instance_ext(nullptr, &n, nullptr) == VK_SUCCESS && n > 0) {
            std::vector<VkExtensionProperties> props(n);
            if (enumerate_instance_ext(nullptr, &n, props.data()) == VK_SUCCESS) {
                for (const auto& p : props) instance_exts.push_back(p.extensionName);
            }
        }
    }

    VkApplicationInfo app{};
    app.sType = VkApplicationInfo::VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "dc-hostprof";
    app.applicationVersion = 1;
    app.apiVersion = loader_version ? loader_version : (1u << 22); // fallback 1.0

    VkInstanceCreateInfo ci{};
    ci.sType = VkInstanceCreateInfo::VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

    VkInstance instance = nullptr;
    VkResult cr = create_instance(&ci, nullptr, &instance);
    if (cr != VK_SUCCESS || !instance) {
        // Loader present but instance creation failed: record loader truth only.
        out.status = "instance_creation_failed";
        char buf[16]; std::snprintf(buf, sizeof(buf), "%d", (int)cr);
        out.api_version = buf; // carry raw VkResult for diagnosis
        return;
    }

    auto enum_phys = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa_fn(instance, "vkEnumeratePhysicalDevices"));
    auto get_props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa_fn(instance, "vkGetPhysicalDeviceProperties"));
    auto get_qfam = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa_fn(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    auto get_mem = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa_fn(instance, "vkGetPhysicalDeviceMemoryProperties"));
    auto enum_dev_ext = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(gipa_fn(instance, "vkEnumerateDeviceExtensionProperties"));
    auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(gipa_fn(instance, "vkDestroyInstance"));

    if (enum_phys && get_props) {
        uint32_t n = 0;
        VkResult er = enum_phys(instance, &n, nullptr);
        if (er != VK_SUCCESS) { out.status = "enum_failed"; destroy(instance, nullptr); FreeLibrary(loader); return; }
        if (n == 0) { out.status = "no_physical_devices"; destroy(instance, nullptr); FreeLibrary(loader); return; }
        if (n > 0) {
            std::vector<VkPhysicalDevice> devs(n);
            if (enum_phys(instance, &n, devs.data()) == VK_SUCCESS) {
                // Report the first discrete-capable device (largest memory heuristic
                // is unreliable without limits queries; the platform records the
                // primary physical device — multi-GPU full enumeration lands with
                // the ConsoleOS renderer work).
                for (const auto& pd : devs) {
                    VkPhysicalDeviceProperties props{};
                    get_props(pd, &props);
                    if (out.device_name.empty()) {
                        out.device_name = props.deviceName;
                        out.api_version = VersionString(props.apiVersion);
                    }
                    // Prefer discrete (deviceType 2 = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                    if (props.deviceType == 2) {
                        out.device_name = props.deviceName;
                        out.api_version = VersionString(props.apiVersion);
                        break;
                    }
                }
                // Use the chosen device for the remaining queries.
                VkPhysicalDevice chosen = devs[0];
                for (const auto& pd : devs) {
                    VkPhysicalDeviceProperties props{};
                    get_props(pd, &props);
                    if (props.deviceType == 2) { chosen = pd; break; }
                }

                if (get_qfam) {
                    uint32_t qn = 0;
                    get_qfam(chosen, &qn, nullptr);
                    out.queue_families = qn;
                }
                if (get_mem) {
                    VkPhysicalDeviceMemoryProperties mem{};
                    get_mem(chosen, &mem);
                    out.memory_heaps = mem.memoryHeapCount;
                }
                if (enum_dev_ext) {
                    uint32_t en = 0;
                    if (enum_dev_ext(chosen, nullptr, &en, nullptr) == VK_SUCCESS && en > 0) {
                        std::vector<VkExtensionProperties> exts(en);
                        if (enum_dev_ext(chosen, nullptr, &en, exts.data()) == VK_SUCCESS) {
                            auto has = [&](const char* name) {
                                for (const auto& e : exts) {
                                    if (std::strcmp(e.extensionName, name) == 0) return true;
                                }
                                return false;
                            };
                            // Extension-presence truth (stable, no struct layouts).
                            out.ray_query = has("VK_KHR_ray_query") || has("VK_KHR_ray_tracing_pipeline");
                            out.mesh_shader = has("VK_EXT_mesh_shader") || has("VK_NV_mesh_shader");
                        }
                    }
                }
            }
        }
        out.available = true;
        out.status = "ok";
    }

    if (destroy) destroy(instance, nullptr);
    FreeLibrary(loader);
}

} // namespace dcwin
