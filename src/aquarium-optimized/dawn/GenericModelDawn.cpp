//
// Copyright (c) 2019 The Aquarium Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "GenericModelDawn.h"

#include <vector>

#include "../Aquarium.h"

GenericModelDawn::GenericModelDawn(Context *context,
                                   Aquarium *aquarium,
                                   MODELGROUP type,
                                   MODELNAME name,
                                   bool blend)
    : Model(type, name, blend), instance(0) {
  mContextDawn = static_cast<ContextDawn *>(context);

  mLightFactorUniforms.shininess = 50.0f;
  mLightFactorUniforms.specularFactor = 1.0f;
}

GenericModelDawn::~GenericModelDawn() {
  mPipeline = nullptr;
  mGroupLayoutModel = nullptr;
  mGroupLayoutPer = nullptr;
  mPipelineLayout = nullptr;
  mBindGroupModel = nullptr;
  mBindGroupPer = nullptr;
  mLightFactorBuffer = nullptr;
  mWorldBuffer = nullptr;
}

void GenericModelDawn::init() {
  mProgramDawn = static_cast<ProgramDawn *>(mProgram);

  mDiffuseTexture = static_cast<TextureDawn *>(textureMap["diffuse"]);
  mNormalTexture = static_cast<TextureDawn *>(textureMap["normalMap"]);
  mReflectionTexture = static_cast<TextureDawn *>(textureMap["reflectionMap"]);
  mSkyboxTexture = static_cast<TextureDawn *>(textureMap["skybox"]);

  mPositionBuffer = static_cast<BufferDawn *>(bufferMap["position"]);
  mNormalBuffer = static_cast<BufferDawn *>(bufferMap["normal"]);
  mTexCoordBuffer = static_cast<BufferDawn *>(bufferMap["texCoord"]);
  mTangentBuffer = static_cast<BufferDawn *>(bufferMap["tangent"]);
  mBiNormalBuffer = static_cast<BufferDawn *>(bufferMap["binormal"]);
  mIndicesBuffer = static_cast<BufferDawn *>(bufferMap["indices"]);

  // Generic models use reflection, normal or diffuse shaders, of which
  // groupLayouts are diiferent in texture binding.  MODELGLOBEBASE use diffuse
  // shader though it contains normal and reflection textures.
  std::vector<wgpu::VertexAttributeDescriptor> vertexAttributeDescriptor;
  if (mNormalTexture && mName != MODELNAME::MODELGLOBEBASE) {
    vertexAttributeDescriptor.resize(5);
    vertexAttributeDescriptor[0].format = wgpu::VertexFormat::Float3;
    vertexAttributeDescriptor[0].offset = 0;
    vertexAttributeDescriptor[0].shaderLocation = 0;
    vertexAttributeDescriptor[1].format = wgpu::VertexFormat::Float3;
    vertexAttributeDescriptor[1].offset = 0;
    vertexAttributeDescriptor[1].shaderLocation = 1;
    vertexAttributeDescriptor[2].format = wgpu::VertexFormat::Float2;
    vertexAttributeDescriptor[2].offset = 0;
    vertexAttributeDescriptor[2].shaderLocation = 2;
    vertexAttributeDescriptor[3].format = wgpu::VertexFormat::Float3;
    vertexAttributeDescriptor[3].offset = 0;
    vertexAttributeDescriptor[3].shaderLocation = 3;
    vertexAttributeDescriptor[4].format = wgpu::VertexFormat::Float3;
    vertexAttributeDescriptor[4].offset = 0;
    vertexAttributeDescriptor[4].shaderLocation = 4;
  } else {
    vertexAttributeDescriptor.resize(3);
    vertexAttributeDescriptor[0].format = wgpu::VertexFormat::Float3;
    vertexAttributeDescriptor[0].offset = 0;
    vertexAttributeDescriptor[0].shaderLocation = 0;
    vertexAttributeDescriptor[1].format = wgpu::VertexFormat::Float3;
    vertexAttributeDescriptor[1].offset = 0;
    vertexAttributeDescriptor[1].shaderLocation = 1;
    vertexAttributeDescriptor[2].format = wgpu::VertexFormat::Float2;
    vertexAttributeDescriptor[2].offset = 0;
    vertexAttributeDescriptor[2].shaderLocation = 2;
  }

  // Generic models use reflection, normal or diffuse shaders, of which
  // groupLayouts are diiferent in texture binding.  MODELGLOBEBASE use diffuse
  // shader though it contains normal and reflection textures.
  std::vector<wgpu::VertexBufferLayoutDescriptor> vertexBufferLayoutDescriptor;
  if (mNormalTexture && mName != MODELNAME::MODELGLOBEBASE) {
    vertexBufferLayoutDescriptor.resize(5);
    vertexBufferLayoutDescriptor[0].arrayStride =
        mPositionBuffer->getDataSize();
    vertexBufferLayoutDescriptor[0].stepMode = wgpu::InputStepMode::Vertex;
    vertexBufferLayoutDescriptor[0].attributeCount = 1;
    vertexBufferLayoutDescriptor[0].attributes = &vertexAttributeDescriptor[0];
    vertexBufferLayoutDescriptor[1].arrayStride = mNormalBuffer->getDataSize();
    vertexBufferLayoutDescriptor[1].stepMode = wgpu::InputStepMode::Vertex;
    vertexBufferLayoutDescriptor[1].attributeCount = 1;
    vertexBufferLayoutDescriptor[1].attributes = &vertexAttributeDescriptor[1];
    vertexBufferLayoutDescriptor[2].arrayStride =
        mTexCoordBuffer->getDataSize();
    vertexBufferLayoutDescriptor[2].stepMode = wgpu::InputStepMode::Vertex;
    vertexBufferLayoutDescriptor[2].attributeCount = 1;
    vertexBufferLayoutDescriptor[2].attributes = &vertexAttributeDescriptor[2];
    vertexBufferLayoutDescriptor[3].arrayStride = mTangentBuffer->getDataSize();
    vertexBufferLayoutDescriptor[3].stepMode = wgpu::InputStepMode::Vertex;
    vertexBufferLayoutDescriptor[3].attributeCount = 1;
    vertexBufferLayoutDescriptor[3].attributes = &vertexAttributeDescriptor[3];
    vertexBufferLayoutDescriptor[4].arrayStride =
        mBiNormalBuffer->getDataSize();
    vertexBufferLayoutDescriptor[4].stepMode = wgpu::InputStepMode::Vertex;
    vertexBufferLayoutDescriptor[4].attributeCount = 1;
    vertexBufferLayoutDescriptor[4].attributes = &vertexAttributeDescriptor[4];
  } else {
    vertexBufferLayoutDescriptor.resize(3);
    vertexBufferLayoutDescriptor[0].arrayStride =
        mPositionBuffer->getDataSize();
    vertexBufferLayoutDescriptor[0].stepMode = wgpu::InputStepMode::Vertex;
    vertexBufferLayoutDescriptor[0].attributeCount = 1;
    vertexBufferLayoutDescriptor[0].attributes = &vertexAttributeDescriptor[0];
    vertexBufferLayoutDescriptor[1].arrayStride = mNormalBuffer->getDataSize();
    vertexBufferLayoutDescriptor[1].stepMode = wgpu::InputStepMode::Vertex;
    vertexBufferLayoutDescriptor[1].attributeCount = 1;
    vertexBufferLayoutDescriptor[1].attributes = &vertexAttributeDescriptor[1];
    vertexBufferLayoutDescriptor[2].arrayStride =
        mTexCoordBuffer->getDataSize();
    vertexBufferLayoutDescriptor[2].stepMode = wgpu::InputStepMode::Vertex;
    vertexBufferLayoutDescriptor[2].attributeCount = 1;
    vertexBufferLayoutDescriptor[2].attributes = &vertexAttributeDescriptor[2];
  }

  mVertexStateDescriptor.vertexBufferCount =
      static_cast<uint32_t>(vertexBufferLayoutDescriptor.size());
  mVertexStateDescriptor.vertexBuffers = vertexBufferLayoutDescriptor.data();

  if (mSkyboxTexture && mReflectionTexture &&
      mName != MODELNAME::MODELGLOBEBASE) {
    mGroupLayoutModel = mContextDawn->MakeBindGroupLayout({
        {0, wgpu::ShaderStage::Fragment, wgpu::BindingType::UniformBuffer},
        {1, wgpu::ShaderStage::Fragment, wgpu::BindingType::Sampler},
        {2, wgpu::ShaderStage::Fragment, wgpu::BindingType::Sampler},
        {3, wgpu::ShaderStage::Fragment, wgpu::BindingType::SampledTexture,
         false, 0, false, wgpu::TextureViewDimension::e2D,
         wgpu::TextureComponentType::Float},
        {4, wgpu::ShaderStage::Fragment, wgpu::BindingType::SampledTexture,
         false, 0, false, wgpu::TextureViewDimension::e2D,
         wgpu::TextureComponentType::Float},
        {5, wgpu::ShaderStage::Fragment, wgpu::BindingType::SampledTexture,
         false, 0, false, wgpu::TextureViewDimension::e2D,
         wgpu::TextureComponentType::Float},
        {6, wgpu::ShaderStage::Fragment, wgpu::BindingType::SampledTexture,
         false, 0, false, wgpu::TextureViewDimension::Cube,
         wgpu::TextureComponentType::Float},
    });
  } else if (mNormalTexture && mName != MODELNAME::MODELGLOBEBASE) {
    mGroupLayoutModel = mContextDawn->MakeBindGroupLayout({
        {0, wgpu::ShaderStage::Fragment, wgpu::BindingType::UniformBuffer},
        {1, wgpu::ShaderStage::Fragment, wgpu::BindingType::Sampler},
        {2, wgpu::ShaderStage::Fragment, wgpu::BindingType::SampledTexture,
         false, 0, false, wgpu::TextureViewDimension::e2D,
         wgpu::TextureComponentType::Float},
        {3, wgpu::ShaderStage::Fragment, wgpu::BindingType::SampledTexture,
         false, 0, false, wgpu::TextureViewDimension::e2D,
         wgpu::TextureComponentType::Float},
    });
  } else {
    mGroupLayoutModel = mContextDawn->MakeBindGroupLayout({
        {0, wgpu::ShaderStage::Fragment, wgpu::BindingType::UniformBuffer},
        {1, wgpu::ShaderStage::Fragment, wgpu::BindingType::Sampler},
        {2, wgpu::ShaderStage::Fragment, wgpu::BindingType::SampledTexture,
         false, 0, false, wgpu::TextureViewDimension::e2D,
         wgpu::TextureComponentType::Float},
    });
  }

  mGroupLayoutPer = mContextDawn->MakeBindGroupLayout({
      {0, wgpu::ShaderStage::Vertex, wgpu::BindingType::UniformBuffer},
  });

  mPipelineLayout = mContextDawn->MakeBasicPipelineLayout({
      mContextDawn->groupLayoutGeneral,
      mContextDawn->groupLayoutWorld,
      mGroupLayoutModel,
      mGroupLayoutPer,
  });

  mPipeline = mContextDawn->createRenderPipeline(
      mPipelineLayout, mProgramDawn, mVertexStateDescriptor, mBlend);

  mLightFactorBuffer = mContextDawn->createBufferFromData(
      &mLightFactorUniforms, sizeof(mLightFactorUniforms),
      sizeof(mLightFactorUniforms),
      wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform);
  mWorldBuffer = mContextDawn->createBufferFromData(
      &mWorldUniformPer, sizeof(mWorldUniformPer), sizeof(mWorldUniformPer),
      wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform);

  // Generic models use reflection, normal or diffuse shaders, of which
  // grouplayouts are diiferent in texture binding. MODELGLOBEBASE use diffuse
  // shader though it contains normal and reflection textures.
  if (mSkyboxTexture && mReflectionTexture &&
      mName != MODELNAME::MODELGLOBEBASE) {
    mBindGroupModel = mContextDawn->makeBindGroup(
        mGroupLayoutModel,
        {{0, mLightFactorBuffer, 0, sizeof(LightFactorUniforms), {}, {}},
         {1, {}, 0, 0, mReflectionTexture->getSampler(), {}},
         {2, {}, 0, 0, mSkyboxTexture->getSampler(), {}},
         {3, {}, 0, 0, {}, mDiffuseTexture->getTextureView()},
         {4, {}, 0, 0, {}, mNormalTexture->getTextureView()},
         {5, {}, 0, 0, {}, mReflectionTexture->getTextureView()},
         {6, {}, 0, 0, {}, mSkyboxTexture->getTextureView()}});
  } else if (mNormalTexture && mName != MODELNAME::MODELGLOBEBASE) {
    mBindGroupModel = mContextDawn->makeBindGroup(
        mGroupLayoutModel,
        {
            {0, mLightFactorBuffer, 0, sizeof(LightFactorUniforms), {}, {}},
            {1, {}, 0, 0, mDiffuseTexture->getSampler(), {}},
            {2, {}, 0, 0, {}, mDiffuseTexture->getTextureView()},
            {3, {}, 0, 0, {}, mNormalTexture->getTextureView()},
        });
  } else {
    mBindGroupModel = mContextDawn->makeBindGroup(
        mGroupLayoutModel,
        {
            {0, mLightFactorBuffer, 0, sizeof(LightFactorUniforms), {}, {}},
            {1, {}, 0, 0, mDiffuseTexture->getSampler(), {}},
            {2, {}, 0, 0, {}, mDiffuseTexture->getTextureView()},
        });
  }

  mBindGroupPer = mContextDawn->makeBindGroup(
      mGroupLayoutPer,
      {
          {0, mWorldBuffer, 0, sizeof(WorldUniformPer), {}, {}},
      });

  mContextDawn->setBufferData(mLightFactorBuffer, sizeof(LightFactorUniforms),
                              &mLightFactorUniforms,
                              sizeof(LightFactorUniforms));
}

void GenericModelDawn::prepareForDraw() {
  mContextDawn->updateBufferData(mWorldBuffer, sizeof(WorldUniformPer),
                                 &mWorldUniformPer, sizeof(WorldUniformPer));
}

void GenericModelDawn::draw() {
  wgpu::RenderPassEncoder pass = mContextDawn->getRenderPass();
  pass.SetPipeline(mPipeline);
  pass.SetBindGroup(0, mContextDawn->bindGroupGeneral, 0, nullptr);
  pass.SetBindGroup(1, mContextDawn->bindGroupWorld, 0, nullptr);
  pass.SetBindGroup(2, mBindGroupModel, 0, nullptr);
  pass.SetBindGroup(3, mBindGroupPer, 0, nullptr);
  pass.SetVertexBuffer(0, mPositionBuffer->getBuffer());
  pass.SetVertexBuffer(1, mNormalBuffer->getBuffer());
  pass.SetVertexBuffer(2, mTexCoordBuffer->getBuffer());
  // diffuseShader doesn't have to input tangent buffer or binormal buffer.
  if (mTangentBuffer && mBiNormalBuffer && mName != MODELNAME::MODELGLOBEBASE) {
    pass.SetVertexBuffer(3, mTangentBuffer->getBuffer());
    pass.SetVertexBuffer(4, mBiNormalBuffer->getBuffer());
  }
  pass.SetIndexBufferWithFormat(mIndicesBuffer->getBuffer(),
                                wgpu::IndexFormat::Uint16, 0);
  pass.DrawIndexed(mIndicesBuffer->getTotalComponents(), instance, 0, 0, 0);
  instance = 0;
}

void GenericModelDawn::updatePerInstanceUniforms(
    const WorldUniforms &worldUniforms) {
  mWorldUniformPer.WorldUniforms[instance] = worldUniforms;

  instance++;
}
