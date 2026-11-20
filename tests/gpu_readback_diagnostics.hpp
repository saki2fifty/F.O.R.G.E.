#pragma once
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include <cstdint>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
// Native declarations must precede the native Diligent interface.
#include <d3d12.h>
#include <wrl/client.h>

#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngineD3D12/interface/RenderDeviceD3D12.h"
#endif

namespace forge::test_gpu {
// Capture native status before exception unwinding releases the device. Debug
// messages observed after destruction cannot establish the original failure.
inline void require_readback(bool ready, Diligent::IRenderDevice* device, const char* operation) {
    if (ready)
        return;
    std::ostringstream message;
    message << operation;
    if (device)
        message << "; backend=" << unsigned(device->GetDeviceInfo().Type);
#ifdef _WIN32
    if (device && device->GetDeviceInfo().Type == Diligent::RENDER_DEVICE_TYPE_D3D12) {
        Diligent::RefCntAutoPtr<Diligent::IRenderDeviceD3D12> native{
            device, Diligent::IID_RenderDeviceD3D12};
        if (native) {
            const auto reason = native->GetD3D12Device()->GetDeviceRemovedReason();
            message << "; GetDeviceRemovedReason=0x" << std::hex << std::uint32_t(reason)
                    << std::dec;
#ifdef __ID3D12DeviceRemovedExtendedData1_INTERFACE_DEFINED__
            if (FAILED(reason)) {
                Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
                if (SUCCEEDED(native->GetD3D12Device()->QueryInterface(IID_PPV_ARGS(&dred)))) {
                    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
                    if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs))) {
                        auto* node = breadcrumbs.pHeadAutoBreadcrumbNode;
                        for (unsigned index = 0; node && index < 8; ++index, node = node->pNext) {
                            unsigned last =
                                node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
                            if (last > node->BreadcrumbCount)
                                last = node->BreadcrumbCount;
                            message << "; breadcrumb="
                                    << (node->pCommandListDebugNameA ? node->pCommandListDebugNameA
                                                                     : "unnamed")
                                    << ':' << last << '/' << node->BreadcrumbCount;
                            if (last < node->BreadcrumbCount && node->pCommandHistory)
                                message << ",next_op=" << unsigned(node->pCommandHistory[last]);
                        }
                    }
                    D3D12_DRED_PAGE_FAULT_OUTPUT1 fault{};
                    if (SUCCEEDED(dred->GetPageFaultAllocationOutput1(&fault)))
                        message << "; page_fault_va=0x" << std::hex << fault.PageFaultVA;
                }
            }
#endif
        }
    }
#endif
    throw std::runtime_error(message.str());
}
} // namespace forge::test_gpu
