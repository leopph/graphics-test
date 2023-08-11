#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d12.h>
#include <d3dx12.h>
#include <dxgi1_6.h>
#include <Windows.h>
#include <wrl/client.h>

#include <array>
#include <cassert>
#include <cstring>
#include <memory>
#include <type_traits>

#ifndef NDEBUG
#include "shaders/generated/PSBinDebug.h"
#include "shaders/generated/VSBinDebug.h"
#else
#include "shaders/generated/VSBin.h"
#include "shaders/generated/PSBin.h"
#endif


namespace {
  auto CALLBACK WindowProc(HWND const hwnd, UINT const msg, WPARAM const wparam, LPARAM const lparam) -> LRESULT {
    if (msg == WM_CLOSE) {
      PostQuitMessage(0);
      return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
}


auto WINAPI wWinMain(_In_ HINSTANCE const hInstance, [[maybe_unused]] _In_opt_ HINSTANCE const hPrevInstance,
                     [[maybe_unused]] _In_ wchar_t* const lpCmdLine, _In_ int const nShowCmd) -> int {
  WNDCLASSW const windowClass{
    .lpfnWndProc = &WindowProc,
    .hInstance = hInstance,
    .hCursor = LoadCursorW(nullptr, IDC_ARROW),
    .lpszClassName = L"D3D12Test"
  };

  auto const result{RegisterClassW(&windowClass)};
  assert(result);

  std::unique_ptr<std::remove_pointer_t<HWND>, decltype([](HWND const hwnd) -> void {
    if (hwnd) {
      DestroyWindow(hwnd);
    }
  })> const hwnd{
    CreateWindowExW(0, windowClass.lpszClassName, L"D3D12Test", WS_POPUP, 0, 0, GetSystemMetrics(SM_CXSCREEN),
                    GetSystemMetrics(SM_CYSCREEN), nullptr, nullptr, hInstance, nullptr)
  };

  assert(hwnd);

  ShowWindow(hwnd.get(), nShowCmd);

  HRESULT hr{};

  using Microsoft::WRL::ComPtr;

#ifndef NDEBUG
  {
    ComPtr<ID3D12Debug5> debug;
    hr = D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf()));
    assert(SUCCEEDED(hr));

    debug->EnableDebugLayer();
  }
#endif

  UINT dxgiFactoryFlags{0};
#ifndef NDEBUG
  dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

  ComPtr<IDXGIFactory7> factory;
  hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(factory.GetAddressOf()));
  assert(SUCCEEDED(hr));

  ComPtr<ID3D12Device9> device;
  hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device.GetAddressOf()));
  assert(SUCCEEDED(hr));

#ifndef NDEBUG
  {
    ComPtr<ID3D12InfoQueue> infoQueue;
    hr = device.As(&infoQueue);
    assert(SUCCEEDED(hr));

    hr = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
    assert(SUCCEEDED(hr));

    hr = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    assert(SUCCEEDED(hr));
  }
#endif

  ComPtr<ID3D12CommandQueue> commandQueue;

  {
    D3D12_COMMAND_QUEUE_DESC constexpr commandQueueDesc{
      .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
      .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
      .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
      .NodeMask = 0
    };

    hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue.GetAddressOf()));
    assert(SUCCEEDED(hr));
  }

  constexpr auto SWAP_CHAIN_BUFFER_COUNT{2};
  constexpr auto SWAP_CHAIN_FORMAT{DXGI_FORMAT_R8G8B8A8_UNORM};
  ComPtr<IDXGISwapChain4> swapChain;

  {
    DXGI_SWAP_CHAIN_DESC1 constexpr swapChainDesc{
      .Width = 0,
      .Height = 0,
      .Format = SWAP_CHAIN_FORMAT,
      .Stereo = FALSE,
      .SampleDesc = {.Count = 1, .Quality = 0},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = SWAP_CHAIN_BUFFER_COUNT,
      .Scaling = DXGI_SCALING_NONE,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
      .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
      .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
    };

    ComPtr<IDXGISwapChain1> tmpSwapChain;
    hr = factory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd.get(), &swapChainDesc, nullptr, nullptr,
                                         tmpSwapChain.GetAddressOf());
    assert(SUCCEEDED(hr));

    hr = tmpSwapChain.As(&swapChain);
    assert(SUCCEEDED(hr));
  }

  auto backBufIdx{swapChain->GetCurrentBackBufferIndex()};

  ComPtr<ID3D12DescriptorHeap> rtvHeap;

  {
    D3D12_DESCRIPTOR_HEAP_DESC constexpr rtvHeapDesc{
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      .NumDescriptors = 2,
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
      .NodeMask = 0
    };

    hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap.GetAddressOf()));
    assert(SUCCEEDED(hr));
  }

  auto const rtvHeapInc{device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV)};
  auto const rtvHeapCpuStart{rtvHeap->GetCPUDescriptorHandleForHeapStart()};

  std::array<ComPtr<ID3D12Resource2>, SWAP_CHAIN_BUFFER_COUNT> backBuffers;
  std::array<CD3DX12_CPU_DESCRIPTOR_HANDLE, SWAP_CHAIN_BUFFER_COUNT> backBufferRTVs{};

  for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++) {
    hr = swapChain->GetBuffer(i, IID_PPV_ARGS(backBuffers[i].GetAddressOf()));
    assert(SUCCEEDED(hr));

    backBufferRTVs[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE{rtvHeapCpuStart, static_cast<INT>(i), rtvHeapInc};

    D3D12_RENDER_TARGET_VIEW_DESC constexpr rtvDesc{
      .Format = SWAP_CHAIN_FORMAT,
      .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
      .Texture2D = {.MipSlice = 0, .PlaneSlice = 0}
    };

    device->CreateRenderTargetView(backBuffers[i].Get(), &rtvDesc, backBufferRTVs[i]);
  }

  constexpr auto MAX_FRAMES_IN_FLIGHT{2};
  UINT64 thisFrameFenceValue{MAX_FRAMES_IN_FLIGHT - 1};

  ComPtr<ID3D12Fence1> fence;
  hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
  assert(SUCCEEDED(hr));

  auto const signalAndWaitFence{
    [&](UINT64 const signalValue, UINT64 const waitValue) {
      hr = commandQueue->Signal(fence.Get(), signalValue);
      assert(SUCCEEDED(hr));

      if (fence->GetCompletedValue() < waitValue) {
        hr = fence->SetEventOnCompletion(waitValue, nullptr);
        assert(SUCCEEDED(hr));
      }
    }
  };

  auto const waitForGpuCompletion{
    [&] {
      auto const signalValue{++thisFrameFenceValue};
      auto const waitValue{signalValue};
      signalAndWaitFence(signalValue, waitValue);
    }
  };

  auto const waitForInFlightFrames{
    [&] {
      auto const signalValue{++thisFrameFenceValue};
      auto const waitValue{signalValue - MAX_FRAMES_IN_FLIGHT + 1};
      signalAndWaitFence(signalValue, waitValue);
    }
  };

  UINT64 frameIdx{0};

  std::array<ComPtr<ID3D12CommandAllocator>, MAX_FRAMES_IN_FLIGHT> cmdAllocators;
  std::array<ComPtr<ID3D12GraphicsCommandList6>, MAX_FRAMES_IN_FLIGHT> cmdLists;

  for (auto i{0}; i < MAX_FRAMES_IN_FLIGHT; i++) {
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(cmdAllocators[i].GetAddressOf()));
    assert(SUCCEEDED(hr));

    hr = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE,
                                    IID_PPV_ARGS(cmdLists[i].GetAddressOf()));
    assert(SUCCEEDED(hr));
  }

  ComPtr<ID3D12RootSignature> rootSig;

  {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC constexpr rootSigDesc{
      .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
      .Desc_1_1 = {
        .NumParameters = 0,
        .pParameters = nullptr,
        .NumStaticSamplers = 0,
        .pStaticSamplers = nullptr,
        .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
      }
    };

    ComPtr<ID3DBlob> rootSigBlob;
    hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, rootSigBlob.GetAddressOf(), nullptr);
    assert(SUCCEEDED(hr));

    hr = device->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
                                     IID_PPV_ARGS(rootSig.GetAddressOf()));
    assert(SUCCEEDED(hr));
  }

  ComPtr<ID3D12PipelineState> pso;

  {
    D3D12_INPUT_ELEMENT_DESC constexpr inputElementDesc{
      .SemanticName = "POSITION",
      .SemanticIndex = 0,
      .Format = DXGI_FORMAT_R32G32_FLOAT,
      .InputSlot = 0,
      .AlignedByteOffset = 0,
      .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
      .InstanceDataStepRate = 0
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC const psoDesc{
      .pRootSignature = rootSig.Get(),
      .VS = CD3DX12_SHADER_BYTECODE{gVSBin, ARRAYSIZE(gVSBin)},
      .PS = CD3DX12_SHADER_BYTECODE{gPSBin, ARRAYSIZE(gPSBin)},
      .BlendState = CD3DX12_BLEND_DESC{D3D12_DEFAULT},
      .SampleMask = UINT_MAX,
      .RasterizerState = CD3DX12_RASTERIZER_DESC{D3D12_DEFAULT},
      .InputLayout = {.pInputElementDescs = &inputElementDesc, .NumElements = 1},
      .IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
      .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
      .NumRenderTargets = 1,
      .RTVFormats = {SWAP_CHAIN_FORMAT},
      .SampleDesc = {.Count = 1, .Quality = 0},
      .NodeMask = 0,
      .CachedPSO = {nullptr, 0},
      .Flags = D3D12_PIPELINE_STATE_FLAG_NONE
    };

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pso.GetAddressOf()));
    assert(SUCCEEDED(hr));
  }

  using Vec2 = std::array<float, 2>;

  std::array constexpr vertices{
    Vec2{0, 0.5f},
    Vec2{0.5f, -0.5f},
    Vec2{-0.5f, -0.5f}
  };

  CD3DX12_HEAP_PROPERTIES const defaultHeapProps{D3D12_HEAP_TYPE_DEFAULT};
  auto const vertBufDesc{CD3DX12_RESOURCE_DESC::Buffer(sizeof vertices)};

  ComPtr<ID3D12Resource> vertexBuffer;
  hr = device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &vertBufDesc,
                                       D3D12_RESOURCE_STATE_COMMON, nullptr,
                                       IID_PPV_ARGS(vertexBuffer.GetAddressOf()));
  assert(SUCCEEDED(hr));

  {
    CD3DX12_HEAP_PROPERTIES const uploadHeapProps{D3D12_HEAP_TYPE_UPLOAD};

    ComPtr<ID3D12Resource> vertexUploadBuffer;
    hr = device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &vertBufDesc,
                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                         IID_PPV_ARGS(vertexUploadBuffer.GetAddressOf()));
    assert(SUCCEEDED(hr));

    void* mappedVertexUploadBuffer;
    hr = vertexUploadBuffer->Map(0, nullptr, &mappedVertexUploadBuffer);
    assert(SUCCEEDED(hr));

    std::memcpy(mappedVertexUploadBuffer, vertices.data(), sizeof vertices);
    vertexUploadBuffer->Unmap(0, nullptr);

    hr = cmdLists[frameIdx]->Reset(cmdAllocators[frameIdx].Get(), pso.Get());
    assert(SUCCEEDED(hr));

    cmdLists[frameIdx]->CopyResource(vertexBuffer.Get(), vertexUploadBuffer.Get());

    auto const uploadBarrier{
      CD3DX12_RESOURCE_BARRIER::Transition(vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                           D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
    };

    cmdLists[frameIdx]->ResourceBarrier(1, &uploadBarrier);

    hr = cmdLists[frameIdx]->Close();
    assert(SUCCEEDED(hr));

    commandQueue->ExecuteCommandLists(1, std::array<ID3D12CommandList*, 1>{cmdLists[frameIdx].Get()}.data());
    waitForGpuCompletion();
  }

  D3D12_VERTEX_BUFFER_VIEW const vertexBufferView{
    .BufferLocation = vertexBuffer->GetGPUVirtualAddress(),
    .SizeInBytes = sizeof vertices,
    .StrideInBytes = sizeof(decltype(vertices)::value_type)
  };

  while (true) {
    MSG msg;

    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        waitForGpuCompletion();
        return static_cast<int>(msg.wParam);
      }

      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    hr = cmdAllocators[frameIdx]->Reset();
    assert(SUCCEEDED(hr));

    hr = cmdLists[frameIdx]->Reset(cmdAllocators[frameIdx].Get(), pso.Get());
    assert(SUCCEEDED(hr));

    cmdLists[frameIdx]->SetGraphicsRootSignature(rootSig.Get());

    CD3DX12_VIEWPORT const viewport{backBuffers[backBufIdx].Get()};
    cmdLists[frameIdx]->RSSetViewports(1, &viewport);

    CD3DX12_RECT const scissorRect{
      static_cast<LONG>(viewport.TopLeftX), static_cast<LONG>(viewport.TopLeftY),
      static_cast<LONG>(viewport.TopLeftX + viewport.Width), static_cast<LONG>(viewport.TopLeftY + viewport.Height)
    };
    cmdLists[frameIdx]->RSSetScissorRects(1, &scissorRect);

    auto const swapChainRtvBarrier{
      CD3DX12_RESOURCE_BARRIER::Transition(backBuffers[backBufIdx].Get(), D3D12_RESOURCE_STATE_PRESENT,
                                           D3D12_RESOURCE_STATE_RENDER_TARGET)
    };
    cmdLists[frameIdx]->ResourceBarrier(1, &swapChainRtvBarrier);

    float constexpr clearColor[]{0.2f, 0.3f, 0.3f, 1.f};
    cmdLists[frameIdx]->ClearRenderTargetView(backBufferRTVs[backBufIdx], clearColor, 0, nullptr);
    cmdLists[frameIdx]->OMSetRenderTargets(1, &backBufferRTVs[backBufIdx], FALSE, nullptr);

    cmdLists[frameIdx]->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdLists[frameIdx]->IASetVertexBuffers(0, 1, &vertexBufferView);
    cmdLists[frameIdx]->DrawInstanced(3, 1, 0, 0);

    auto const swapChainPresentBarrier{
      CD3DX12_RESOURCE_BARRIER::Transition(backBuffers[backBufIdx].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                           D3D12_RESOURCE_STATE_PRESENT)
    };
    cmdLists[frameIdx]->ResourceBarrier(1, &swapChainPresentBarrier);

    hr = cmdLists[frameIdx]->Close();
    assert(SUCCEEDED(hr));

    commandQueue->ExecuteCommandLists(1, std::array<ID3D12CommandList*, 1>{cmdLists[frameIdx].Get()}.data());

    hr = swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
    assert(SUCCEEDED(hr));

    waitForInFlightFrames();
    backBufIdx = swapChain->GetCurrentBackBufferIndex();
    frameIdx = (frameIdx + 1) % MAX_FRAMES_IN_FLIGHT;
  }
}