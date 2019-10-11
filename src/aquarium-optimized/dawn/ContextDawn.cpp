//
// Copyright (c) 2019 The Aquarium Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// DeviceDawn.cpp: Implements accessing functions to the graphics API of Dawn.

#include <array>
#include <iostream>
#include <string>
#include <vector>
#include <dawn/dawn.h>
#include <dawn/dawn_wsi.h>
#include <dawn/dawncpp.h>
#include <dawn_native/DawnNative.h>
#include <shaderc/shaderc.hpp>

#include "BufferDawn.h"
#include "ContextDawn.h"
#include "FishModelDawn.h"
#include "FishModelInstancedDrawDawn.h"
#include "GenericModelDawn.h"
#include "InnerModelDawn.h"
#include "OutsideModelDawn.h"
#include "ProgramDawn.h"
#include "SeaweedModelDawn.h"
#include "TextureDawn.h"

#include "common/AQUARIUM_ASSERT.h"
#include "common/Constants.h"
#include "imgui_impl_dawn.h"
#include "imgui_impl_glfw.h"
#include "utils/BackendBinding.h"
#include "utils/ComboRenderPipelineDescriptor.h"
#include "utils/SystemUtils.h"

#include "../Aquarium.h"

ContextDawn::ContextDawn(BACKENDTYPE backendType)
    : queue(nullptr),
      groupLayoutGeneral(nullptr),
      bindGroupGeneral(nullptr),
      groupLayoutWorld(nullptr),
      bindGroupWorld(nullptr),
      groupLayoutFishPer(nullptr),
      fishPersBuffer(nullptr),
      bindGroupFishPers(nullptr),
      stagingBuffer(nullptr),
      fishPers(nullptr),
      mDevice(nullptr),
      mWindow(nullptr),
      mInstance(),
      mSwapchain(nullptr),
      mCommandEncoder(nullptr),
      mRenderPass(nullptr),
      mRenderPassDescriptor({}),
      mBackbuffer(nullptr),
      mSceneRenderTargetView(nullptr),
      mSceneDepthStencilView(nullptr),
      mPipeline(nullptr),
      mBindGroup(nullptr),
      mPreferredSwapChainFormat(dawn::TextureFormat::RGBA8Unorm),
      mEnableMSAA(false),
      mappedData(nullptr)
{
    mResourceHelper = new ResourceHelper("dawn", "", backendType);
    initAvailableToggleBitset(backendType);
}

ContextDawn::~ContextDawn()
{
    delete mResourceHelper;
    if (mWindow != nullptr)
    {
        destoryImgUI();
    }

    mSceneRenderTargetView   = nullptr;
    mSceneDepthStencilView   = nullptr;
    mBackbuffer              = nullptr;
    mPipeline                = nullptr;
    mBindGroup               = nullptr;
    mLightWorldPositionBuffer = nullptr;
    mLightBuffer              = nullptr;
    mFogBuffer                = nullptr;
    mCommandEncoder          = nullptr;
    mCommandBuffers.clear();
    mRenderPass              = nullptr;
    mRenderPassDescriptor    = {};
    groupLayoutGeneral       = nullptr;
    bindGroupGeneral         = nullptr;
    groupLayoutWorld         = nullptr;
    bindGroupWorld           = nullptr;

    groupLayoutFishPer = nullptr;
    destoryFishResource();

    stagingBuffer = nullptr;

    mSwapchain               = nullptr;
    queue                    = nullptr;
    mDevice                  = nullptr;
}

bool ContextDawn::initialize(
    BACKENDTYPE backend,
    const std::bitset<static_cast<size_t>(TOGGLE::TOGGLEMAX)> &toggleBitset)
{
    dawn_native::BackendType backendType = dawn_native::BackendType::Null;

    switch (backend)
    {
        case BACKENDTYPE::BACKENDTYPEDAWND3D12:
        {
            backendType = dawn_native::BackendType::D3D12;
            break;
        }
        case BACKENDTYPE::BACKENDTYPEDAWNVULKAN:
        {
            backendType = dawn_native::BackendType::Vulkan;
            break;
        }
        case BACKENDTYPE::BACKENDTYPEDAWNMETAL:
        {
            backendType = dawn_native::BackendType::Metal;
            break;
        }
        case BACKENDTYPE::BACKENDTYPEOPENGL:
        {
            backendType = dawn_native::BackendType::OpenGL;
            break;
        }
        default:
        {
            std::cerr << "Backend type can not reached." << std::endl;
            return false;
        }
    }

    mEnableMSAA = toggleBitset.test(static_cast<size_t>(TOGGLE::ENABLEMSAAx4));

    // initialise GLFW
    if (!glfwInit())
    {
        std::cout << "Failed to initialise GLFW" << std::endl;
        return false;
    }

    utils::SetupGLFWWindowHintsForBackend(backendType);
    // set full screen
    glfwWindowHint(GLFW_VISIBLE, GL_FALSE);

    GLFWmonitor *pMonitor   = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(pMonitor);
    mClientWidth            = mode->width;
    mClientHeight           = mode->height;

    if (toggleBitset.test(static_cast<size_t>(TOGGLE::ENABLEFULLSCREENMODE)))
    {
        mWindow = glfwCreateWindow(mClientWidth, mClientHeight, "Aquarium", pMonitor, nullptr);
    }
    else
    {
        mWindow = glfwCreateWindow(mClientWidth, mClientHeight, "Aquarium", nullptr, nullptr);
    }

    if (mWindow == nullptr)
    {
        std::cout << "Failed to open GLFW window." << std::endl;
        glfwTerminate();
        return false;
    }

    // Get the resolution of screen
    glfwGetFramebufferSize(mWindow, &mClientWidth, &mClientHeight);

    mInstance = std::make_unique<dawn_native::Instance>();
    utils::DiscoverAdapter(mInstance.get(), mWindow, backendType);

    dawn_native::Adapter backendAdapter;
    if (!GetHardwareAdapter(mInstance, &backendAdapter, backendType, toggleBitset))
    {
        return false;
    }

    DawnDevice backendDevice   = backendAdapter.CreateDevice();
    DawnProcTable backendProcs = dawn_native::GetProcs();

    utils::BackendBinding *binding = utils::CreateBinding(backendType, mWindow, backendDevice);
    if (binding == nullptr)
    {
        return false;
    }

    dawnSetProcs(&backendProcs);
    mDevice = dawn::Device::Acquire(backendDevice);

    queue = mDevice.CreateQueue();
    dawn::SwapChainDescriptor swapChainDesc;
    swapChainDesc.implementation = binding->GetSwapChainImplementation();
    mSwapchain                   = mDevice.CreateSwapChain(&swapChainDesc);

    mPreferredSwapChainFormat =
        static_cast<dawn::TextureFormat>(binding->GetPreferredSwapChainTextureFormat());
    mSwapchain.Configure(mPreferredSwapChainFormat, kSwapchainBackBufferUsage, mClientWidth,
                         mClientHeight);

    dawn_native::PCIInfo info = backendAdapter.GetPCIInfo();
    std::string renderer      = info.name;
    std::cout << renderer << std::endl;
    mResourceHelper->setRenderer(renderer);

    // When MSAA is enabled, we create an intermediate multisampled texture to render the scene to.
    if (mEnableMSAA)
    {
        mSceneRenderTargetView = createMultisampledRenderTargetView();
    }

    mSceneDepthStencilView = createDepthStencilView();

    // TODO(jiawei.shao@intel.com): support recreating swapchain when window is resized on all
    // backends
    if (backend == BACKENDTYPE::BACKENDTYPEDAWNVULKAN)
    {
        glfwSetFramebufferSizeCallback(mWindow, framebufferResizeCallback);
        glfwSetWindowUserPointer(mWindow, this);
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer bindings
    // We use glfw to create window for dawn backend as well.
    // Because imgui doesn't have dawn backend, we rewrite the functions by dawn API in
    // imgui_impl_dawn.cpp and imgui_impl_dawn.h
    ImGui_ImplGlfw_InitForOpenGL(mWindow, true);
    ImGui_ImplDawn_Init(this, mPreferredSwapChainFormat, mEnableMSAA);

    return true;
}

void ContextDawn::framebufferResizeCallback(GLFWwindow *window, int width, int height) {
    ContextDawn *contextDawn = reinterpret_cast<ContextDawn *>(glfwGetWindowUserPointer(window));
    contextDawn->mIsSwapchainOutOfDate = true;
}

bool ContextDawn::GetHardwareAdapter(
    std::unique_ptr<dawn_native::Instance> &instance,
    dawn_native::Adapter *backendAdapter,
    dawn_native::BackendType backendType,
    const std::bitset<static_cast<size_t>(TOGGLE::TOGGLEMAX)> &toggleBitset)
{
    bool enableIntegratedGpu = toggleBitset.test(static_cast<size_t>(TOGGLE::INTEGRATEDGPU));
    bool enableDiscreteGpu   = toggleBitset.test(static_cast<size_t>(TOGGLE::DISCRETEGPU));
    bool useDefaultGpu       = (enableDiscreteGpu | enableIntegratedGpu) == false ? true : false;
    bool result             = false;

    // Get an adapter for the backend to use, and create the Device.
    for (auto &adapter : instance->GetAdapters())
    {
        if (adapter.GetBackendType() == backendType)
        {
            if (useDefaultGpu ||
                (enableDiscreteGpu &&
                 adapter.GetDeviceType() == dawn_native::DeviceType::DiscreteGPU) ||
                (enableIntegratedGpu &&
                 adapter.GetDeviceType() == dawn_native::DeviceType::IntegratedGPU))
            {
                *backendAdapter = adapter;
                result          = true;
                break;
            }
        }
    }

    if (!result)
    {
        std::cerr << "Failed to create adapter." << std::endl;
        return false;
    }

    return true;
}

void ContextDawn::initAvailableToggleBitset(BACKENDTYPE backendType)
{
    mAvailableToggleBitset.set(static_cast<size_t>(TOGGLE::ENABLEMSAAx4));
    mAvailableToggleBitset.set(static_cast<size_t>(TOGGLE::ENABLEINSTANCEDDRAWS));
    mAvailableToggleBitset.set(static_cast<size_t>(TOGGLE::ENABLEDYNAMICBUFFEROFFSET));
    mAvailableToggleBitset.set(static_cast<size_t>(TOGGLE::DISCRETEGPU));
    mAvailableToggleBitset.set(static_cast<size_t>(TOGGLE::INTEGRATEDGPU));
    mAvailableToggleBitset.set(static_cast<size_t>(TOGGLE::ENABLEFULLSCREENMODE));
    mAvailableToggleBitset.set(static_cast<size_t>(TOGGLE::BUFFERMAPPINGASYNC));
}

Texture *ContextDawn::createTexture(const std::string &name, const std::string &url)
{
    Texture *texture = new TextureDawn(this, name, url);
    texture->loadTexture();
    return texture;
}

Texture *ContextDawn::createTexture(const std::string &name, const std::vector<std::string> &urls)
{
    Texture *texture = new TextureDawn(this, name, urls);
    texture->loadTexture();
    return texture;
}

dawn::Texture ContextDawn::createTexture(const dawn::TextureDescriptor &descriptor) const
{
    return mDevice.CreateTexture(&descriptor);
}

dawn::Sampler ContextDawn::createSampler(const dawn::SamplerDescriptor &descriptor) const
{
    return mDevice.CreateSampler(&descriptor);
}

dawn::Buffer ContextDawn::createBufferFromData(const void *pixels,
                                               int size,
                                               dawn::BufferUsage usage) const
{
    return utils::CreateBufferFromData(mDevice, pixels, size, usage);
}

dawn::BufferCopyView ContextDawn::createBufferCopyView(const dawn::Buffer &buffer,
    uint32_t offset,
    uint32_t rowPitch,
    uint32_t imageHeight) const {

    return utils::CreateBufferCopyView(buffer, offset, rowPitch, imageHeight);
}

dawn::TextureCopyView ContextDawn::createTextureCopyView(dawn::Texture texture,
                                                         uint32_t level,
                                                         uint32_t slice,
                                                         dawn::Origin3D origin)
{

    return utils::CreateTextureCopyView(texture, level, slice, origin);
}

dawn::CommandBuffer ContextDawn::copyBufferToTexture(const dawn::BufferCopyView &bufferCopyView, const dawn::TextureCopyView &textureCopyView, const dawn::Extent3D& ext3D) const
{
    dawn::CommandEncoder encoder = mDevice.CreateCommandEncoder();
    encoder.CopyBufferToTexture(&bufferCopyView, &textureCopyView, &ext3D);
    dawn::CommandBuffer copy = encoder.Finish();
    return copy;
}

dawn::CommandBuffer ContextDawn::copyBufferToBuffer(dawn::Buffer const &srcBuffer,
                                                    uint64_t srcOffset,
                                                    dawn::Buffer const &destBuffer,
                                                    uint64_t destOffset,
                                                    uint64_t size)
{
    dawn::CommandEncoder encoder = mDevice.CreateCommandEncoder();
    encoder.CopyBufferToBuffer(srcBuffer, srcOffset, destBuffer, destOffset, size);
    dawn::CommandBuffer copy = encoder.Finish();

    return copy;
}

dawn::ShaderModule ContextDawn::createShaderModule(utils::SingleShaderStage stage,
                                                   const std::string &str) const
{
    return utils::CreateShaderModule(mDevice, stage, str.c_str());
}

dawn::BindGroupLayout ContextDawn::MakeBindGroupLayout(
    std::initializer_list<dawn::BindGroupLayoutBinding> bindingsInitializer) const {

    return utils::MakeBindGroupLayout(mDevice, bindingsInitializer);
}

dawn::PipelineLayout ContextDawn::MakeBasicPipelineLayout(
    std::vector<dawn::BindGroupLayout> bindingsInitializer) const {
    dawn::PipelineLayoutDescriptor descriptor;

    descriptor.bindGroupLayoutCount = static_cast<uint32_t>(bindingsInitializer.size());
    descriptor.bindGroupLayouts = bindingsInitializer.data();

    return mDevice.CreatePipelineLayout(&descriptor);
}

dawn::RenderPipeline ContextDawn::createRenderPipeline(
    dawn::PipelineLayout mPipelineLayout,
    ProgramDawn *mProgramDawn,
    const dawn::VertexInputDescriptor &mVertexInputDescriptor,
    bool enableBlend) const
{
    const dawn::ShaderModule &mVsModule = mProgramDawn->getVSModule();
    const dawn::ShaderModule &mFsModule = mProgramDawn->getFSModule();

    dawn::BlendDescriptor blendDescriptor;
    blendDescriptor.operation = dawn::BlendOperation::Add;
    if (enableBlend)
    {
        blendDescriptor.srcFactor = dawn::BlendFactor::SrcAlpha;
        blendDescriptor.dstFactor = dawn::BlendFactor::OneMinusSrcAlpha;
    }
    else
    {
        blendDescriptor.srcFactor = dawn::BlendFactor::One;
        blendDescriptor.dstFactor = dawn::BlendFactor::Zero;
    }

    dawn::ColorStateDescriptor ColorStateDescriptor;
    ColorStateDescriptor.colorBlend     = blendDescriptor;
    ColorStateDescriptor.alphaBlend     = blendDescriptor;
    ColorStateDescriptor.writeMask      = dawn::ColorWriteMask::All;

    dawn::RasterizationStateDescriptor rasterizationState;
    rasterizationState.nextInChain         = nullptr;
    rasterizationState.frontFace           = dawn::FrontFace::CCW;
    rasterizationState.cullMode            = dawn::CullMode::Back;
    rasterizationState.depthBias           = 0;
    rasterizationState.depthBiasSlopeScale = 0.0;
    rasterizationState.depthBiasClamp      = 0.0;

    // test
    utils::ComboRenderPipelineDescriptor descriptor(mDevice);
    descriptor.layout                               = mPipelineLayout;
    descriptor.vertexStage.module                   = mVsModule;
    descriptor.cFragmentStage.module                = mFsModule;
    descriptor.vertexInput                          = &mVertexInputDescriptor;
    descriptor.depthStencilState                    = &descriptor.cDepthStencilState;
    descriptor.cDepthStencilState.format            = dawn::TextureFormat::Depth24PlusStencil8;
    descriptor.colorStateCount                      = 1;
    descriptor.cColorStates[0]                      = ColorStateDescriptor;
    descriptor.cColorStates[0].format               = mPreferredSwapChainFormat;
    descriptor.cDepthStencilState.depthWriteEnabled = true;
    descriptor.cDepthStencilState.depthCompare      = dawn::CompareFunction::Less;
    descriptor.primitiveTopology                    = dawn::PrimitiveTopology::TriangleList;
    descriptor.sampleCount                          = mEnableMSAA ? 4 : 1;
    descriptor.rasterizationState                   = &rasterizationState;

    dawn::RenderPipeline mPipeline = mDevice.CreateRenderPipeline(&descriptor);

    return mPipeline;
}

dawn::TextureView ContextDawn::createMultisampledRenderTargetView() const
{
    dawn::TextureDescriptor descriptor;
    descriptor.dimension       = dawn::TextureDimension::e2D;
    descriptor.size.width      = mClientWidth;
    descriptor.size.height     = mClientHeight;
    descriptor.size.depth      = 1;
    descriptor.arrayLayerCount = 1;
    descriptor.sampleCount     = 4;
    descriptor.format          = mPreferredSwapChainFormat;
    descriptor.mipLevelCount   = 1;
    descriptor.usage           = dawn::TextureUsage::OutputAttachment;

    return mDevice.CreateTexture(&descriptor).CreateView();
}

dawn::TextureView ContextDawn::createDepthStencilView() const
{
    dawn::TextureDescriptor descriptor;
    descriptor.dimension       = dawn::TextureDimension::e2D;
    descriptor.size.width      = mClientWidth;
    descriptor.size.height     = mClientHeight;
    descriptor.size.depth      = 1;
    descriptor.arrayLayerCount = 1;
    descriptor.sampleCount     = mEnableMSAA ? 4 : 1;
    descriptor.format          = dawn::TextureFormat::Depth24PlusStencil8;
    descriptor.mipLevelCount   = 1;
    descriptor.usage           = dawn::TextureUsage::OutputAttachment;
    auto depthStencilTexture   = mDevice.CreateTexture(&descriptor);
    return depthStencilTexture.CreateView();
}

dawn::Buffer ContextDawn::createBuffer(uint32_t size, dawn::BufferUsage bit) const
{
    dawn::BufferDescriptor descriptor;
    descriptor.size = size;
    descriptor.usage = bit;

    dawn::Buffer buffer = mDevice.CreateBuffer(&descriptor);
    return buffer;
}

void ContextDawn::setBufferData(const dawn::Buffer& buffer, uint32_t start, uint32_t size, const void* pixels) const
{
    buffer.SetSubData(start, size, reinterpret_cast<const uint8_t*>(pixels));
}

dawn::BindGroup ContextDawn::makeBindGroup(
    const dawn::BindGroupLayout &layout,
    std::initializer_list<utils::BindingInitializationHelper> bindingsInitializer) const
{
    return utils::MakeBindGroup(mDevice, layout, bindingsInitializer);
}

void ContextDawn::initGeneralResources(Aquarium* aquarium)
{
    // initilize general uniform buffers
    groupLayoutGeneral = MakeBindGroupLayout({
        {0, dawn::ShaderStage::Fragment, dawn::BindingType::UniformBuffer},
        {1, dawn::ShaderStage::Fragment, dawn::BindingType::UniformBuffer},
    });

    mLightBuffer = createBufferFromData(&aquarium->lightUniforms, sizeof(aquarium->lightUniforms),
                                        dawn::BufferUsage::CopyDst | dawn::BufferUsage::Uniform);
    mFogBuffer   = createBufferFromData(&aquarium->fogUniforms, sizeof(aquarium->fogUniforms),
                                      dawn::BufferUsage::CopyDst | dawn::BufferUsage::Uniform);

    bindGroupGeneral =
        makeBindGroup(groupLayoutGeneral, {{0, mLightBuffer, 0, sizeof(aquarium->lightUniforms)},
                                           {1, mFogBuffer, 0, sizeof(aquarium->fogUniforms)}});

    setBufferData(mLightBuffer, 0, sizeof(LightUniforms), &aquarium->lightUniforms);
    setBufferData(mFogBuffer, 0, sizeof(FogUniforms), &aquarium->fogUniforms);

    // initilize world uniform buffers
    groupLayoutWorld = MakeBindGroupLayout({
        {0, dawn::ShaderStage::Vertex, dawn::BindingType::UniformBuffer},
    });

    mLightWorldPositionBuffer = createBufferFromData(
        &aquarium->lightWorldPositionUniform, sizeof(aquarium->lightWorldPositionUniform),
        dawn::BufferUsage::CopyDst | dawn::BufferUsage::Uniform);

    bindGroupWorld = makeBindGroup(
        groupLayoutWorld,
        {
            {0, mLightWorldPositionBuffer, 0, sizeof(aquarium->lightWorldPositionUniform)},
        });

    setBufferData(mLightWorldPositionBuffer, 0, sizeof(LightWorldPositionUniform),
                  &aquarium->lightWorldPositionUniform);

    bool enableDynamicBufferOffset =
        aquarium->toggleBitset.test(static_cast<size_t>(TOGGLE::ENABLEDYNAMICBUFFEROFFSET));
    if (enableDynamicBufferOffset)
    {
        groupLayoutFishPer = MakeBindGroupLayout({
            {0, dawn::ShaderStage::Vertex, dawn::BindingType::UniformBuffer, true},
        });
    }
    else
    {
        groupLayoutFishPer = MakeBindGroupLayout({
            {0, dawn::ShaderStage::Vertex, dawn::BindingType::UniformBuffer},
        });
    }
    reallocResource(aquarium->getPreFishCount(), aquarium->getCurFishCount(),
                    enableDynamicBufferOffset);

    // Create a big buffer for buffer mapping async
    // TODO(yizhou): Use the staging buffer to upload more data, such as vertex buffers and world
    // uniforms
    stagingBuffer = createBuffer(sizeof(FishPer) * aquarium->getCurFishCount(),
                                 dawn::BufferUsage::MapWrite | dawn::BufferUsage::CopySrc);
}

void ContextDawn::updateWorldlUniforms(Aquarium* aquarium)
{
    setBufferData(mLightWorldPositionBuffer, 0, sizeof(LightWorldPositionUniform),
                  &aquarium->lightWorldPositionUniform);
}

Buffer *ContextDawn::createBuffer(int numComponents, std::vector<float> *buf, bool isIndex)
{
    Buffer *buffer = new BufferDawn(this, static_cast<int>(buf->size()), numComponents, buf, isIndex);
    return buffer;
}

Buffer *ContextDawn::createBuffer(int numComponents, std::vector<unsigned short> *buf, bool isIndex)
{
    Buffer *buffer =
        new BufferDawn(this, static_cast<int>(buf->size()), numComponents, buf, isIndex);
    return buffer;
}

Program *ContextDawn::createProgram(const std::string &mVId, const std::string &mFId)
{
    ProgramDawn *program = new ProgramDawn(this, mVId, mFId);
    program->loadProgram();

    return program;
}

void ContextDawn::setWindowTitle(const std::string &text)
{
    glfwSetWindowTitle(mWindow, text.c_str());
}

bool ContextDawn::ShouldQuit()
{
    return glfwWindowShouldClose(mWindow);
}

void ContextDawn::KeyBoardQuit()
{
    if (glfwGetKey(mWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(mWindow, GL_TRUE);
}

// Submit commands of the frame
void ContextDawn::DoFlush(const std::bitset<static_cast<size_t>(TOGGLE::TOGGLEMAX)> &toggleBitset)
{
    mRenderPass.EndPass();
    dawn::CommandBuffer cmd = mCommandEncoder.Finish();
    mCommandBuffers.emplace_back(cmd);

    // Wait for staging buffer uploading
    if (toggleBitset.test(static_cast<size_t>(TOGGLE::BUFFERMAPPINGASYNC)))
    {
        while (mappedData == nullptr)
        {
            WaitABit();
        }
        mappedData = nullptr;

        stagingBuffer.Unmap();
    }

    Flush();

    mSwapchain.Present(mBackbuffer);

    glfwPollEvents();
}

void ContextDawn::Flush()
{
    queue.Submit(mCommandBuffers.size(), mCommandBuffers.data());
    mCommandBuffers.clear();
}

void ContextDawn::Terminate()
{
    glfwTerminate();
}

void ContextDawn::showWindow()
{
    glfwShowWindow(mWindow);
}

void ContextDawn::showFPS(const FPSTimer &fpsTimer, int *fishCount)
{
    // Start the Dear ImGui frame
    ImGui_ImplDawn_NewFrame();
    renderImgui(fpsTimer, fishCount);
    ImGui_ImplDawn_RenderDrawData(ImGui::GetDrawData());
}

void ContextDawn::destoryImgUI()
{
    ImGui_ImplDawn_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void ContextDawn::preFrame()
{
    if (mIsSwapchainOutOfDate) {
        glfwGetFramebufferSize(mWindow, &mClientWidth, &mClientHeight);
        if (mEnableMSAA) {
            mSceneRenderTargetView = createMultisampledRenderTargetView();
        }
        mSceneDepthStencilView = createDepthStencilView();
        mSwapchain.Configure(mPreferredSwapChainFormat, kSwapchainBackBufferUsage, mClientWidth,
                             mClientHeight);

        mIsSwapchainOutOfDate = false;
    }

    mCommandEncoder = mDevice.CreateCommandEncoder();
    mBackbuffer     = mSwapchain.GetNextTexture();

    if (mEnableMSAA)
    {
        // If MSAA is enabled, we render to a multisampled texture and then resolve to the backbuffer
        mRenderPassDescriptor = utils::ComboRenderPassDescriptor({mSceneRenderTargetView},
                                                                 mSceneDepthStencilView);
        mRenderPassDescriptor.cColorAttachments[0].resolveTarget = mBackbuffer.CreateView();
    }
    else
    {
        // When MSAA is off, we render directly to the backbuffer
        mRenderPassDescriptor =
            utils::ComboRenderPassDescriptor({mBackbuffer.CreateView()}, mSceneDepthStencilView);
    }

    mRenderPass = mCommandEncoder.BeginRenderPass(&mRenderPassDescriptor);
}

Model * ContextDawn::createModel(Aquarium* aquarium, MODELGROUP type, MODELNAME name, bool blend)
{
    Model *model;
    switch (type)
    {
    case MODELGROUP::FISH:
        model = new FishModelDawn(this, aquarium, type, name, blend);
        break;
    case MODELGROUP::FISHINSTANCEDDRAW:
        model = new FishModelInstancedDrawDawn(this, aquarium, type, name, blend);
        break;
    case MODELGROUP::GENERIC:
        model = new GenericModelDawn(this, aquarium, type, name, blend);
        break;
    case MODELGROUP::INNER:
        model = new InnerModelDawn(this, aquarium, type, name, blend);
        break;
    case MODELGROUP::SEAWEED:
        model = new SeaweedModelDawn(this, aquarium, type, name, blend);
        break;
    case MODELGROUP::OUTSIDE:
        model = new OutsideModelDawn(this, aquarium, type, name, blend);
        break;
    default:
        model = nullptr;
        std::cout << "can not create model type" << std::endl;
    }

    return model;
}

void ContextDawn::reallocResource(int preTotalInstance,
                                  int curTotalInstance,
                                  bool enableDynamicBufferOffset)
{
    mPreTotalInstance          = preTotalInstance;
    mCurTotalInstance          = curTotalInstance;
    mEnableDynamicBufferOffset = enableDynamicBufferOffset;

    if (curTotalInstance == 0)
        return;

    // If current fish number > pre fish number, allocate a new bigger buffer.
    // If current fish number <= prefish number, do not allocate a new one.
    // TODO(yizhou) : optimize the buffer allocation strategy.
    if (preTotalInstance >= curTotalInstance)
    {
        return;
    }

    destoryFishResource();

    fishPers = new FishPer[curTotalInstance];

    if (enableDynamicBufferOffset)
    {
        bindGroupFishPers = new dawn::BindGroup[1];
    }
    else
    {
        bindGroupFishPers = new dawn::BindGroup[curTotalInstance];
    }

    fishPersBuffer = createBufferFromData(fishPers, sizeof(FishPer) * curTotalInstance,
                                          dawn::BufferUsage::CopyDst | dawn::BufferUsage::Uniform);

    // TODO(yizhou): the staging buffer should be bigger to hold other uniform buffers in the
    // future. But now we only use the buffer to upload fish buffer, so the size is the same as fish
    // buffer.
    stagingBuffer = createBuffer(sizeof(FishPer) * curTotalInstance,
                                 dawn::BufferUsage::MapWrite | dawn::BufferUsage::CopySrc);

    if (enableDynamicBufferOffset)
    {
        bindGroupFishPers[0] =
            makeBindGroup(groupLayoutFishPer, {{0, fishPersBuffer, 0, sizeof(FishPer)}});
    }
    else
    {
        for (int i = 0; i < curTotalInstance; i++)
        {
            bindGroupFishPers[i] = makeBindGroup(
                groupLayoutFishPer, {{0, fishPersBuffer, sizeof(FishPer) * i, sizeof(FishPer)}});
        }
    }
}

void ContextDawn::MapWriteCallback(DawnBufferMapAsyncStatus status,
                                   void *data,
                                   uint64_t,
                                   void *userdata)
{
    ASSERT(status == DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS);
    ASSERT(data != nullptr);
    ContextDawn *contextDawn = static_cast<ContextDawn *>(userdata);

    int size                = contextDawn->mCurTotalInstance;
    contextDawn->mappedData = data;
    memcpy(contextDawn->mappedData, contextDawn->fishPers,
           sizeof(FishPer) * size);

    // Create a command buffer that copy from the staging buffer to dest buffer.
    dawn::CommandBuffer command = contextDawn->copyBufferToBuffer(
        contextDawn->stagingBuffer, 0, contextDawn->fishPersBuffer, 0, sizeof(FishPer) * size);
    contextDawn->mCommandBuffers.emplace_back(command);
}

void ContextDawn::WaitABit()
{
    mDevice.Tick();

    utils::USleep(100);
}

void ContextDawn::updateAllFishData(
    const std::bitset<static_cast<size_t>(TOGGLE::TOGGLEMAX)> &toggleBitset)
{
    if (toggleBitset.test(static_cast<size_t>(TOGGLE::BUFFERMAPPINGASYNC)))
    {
        stagingBuffer.MapWriteAsync(MapWriteCallback, this);
    }
    else
    {
        setBufferData(fishPersBuffer, 0, sizeof(FishPer) * mCurTotalInstance, fishPers);
    }
}

void ContextDawn::destoryFishResource()
{
    fishPersBuffer = nullptr;
    stagingBuffer  = nullptr;

    if (fishPers != nullptr)
    {
        delete fishPers;
        fishPers = nullptr;
    }
    if (mEnableDynamicBufferOffset)
    {
        if (bindGroupFishPers != nullptr)
        {
            if (bindGroupFishPers[0].Get() != nullptr)
            {
                bindGroupFishPers[0] = nullptr;
            }
        }
    }
    else
    {
        if (bindGroupFishPers != nullptr)
        {
            for (int i = 0; i < mPreTotalInstance; i++)
            {
                if (bindGroupFishPers[i].Get() != nullptr)
                {
                    bindGroupFishPers[i] = nullptr;
                }
            }
        }
    }

    bindGroupFishPers = nullptr;
}
