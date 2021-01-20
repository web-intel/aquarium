//
// Copyright (c) 2019 The Aquarium Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// TextureDawn.cpp: Wrap textures of Dawn. Load image files and wrap into a Dawn
// texture.

#include "TextureDawn.h"

#include <algorithm>
#include <cmath>

#include "ContextDawn.h"
#include "common/AQUARIUM_ASSERT.h"

TextureDawn::~TextureDawn() {

  DestoryImageData(mPixelVec);
  DestoryImageData(mResizedVec);
  mTextureView = nullptr;
  mTexture = nullptr;
  mSampler = nullptr;
}

TextureDawn::TextureDawn(ContextDawn *context,
                         const std::string &name,
                         const Path &url)
    : Texture(name, url, true),
      mTextureDimension(wgpu::TextureDimension::e2D),
      mTextureViewDimension(wgpu::TextureViewDimension::e2D),
      mTexture(nullptr),
      mSampler(nullptr),
      mFormat(wgpu::TextureFormat::RGBA8Unorm),
      mTextureView(nullptr),
      mContext(context) {
}

TextureDawn::TextureDawn(ContextDawn *context,
                         const std::string &name,
                         const std::vector<Path> &urls)
    : Texture(name, urls, false),
      mTextureDimension(wgpu::TextureDimension::e2D),
      mTextureViewDimension(wgpu::TextureViewDimension::Cube),
      mFormat(wgpu::TextureFormat::RGBA8Unorm),
      mContext(context) {
}

void TextureDawn::loadTexture() {
  wgpu::SamplerDescriptor samplerDesc = {};
  const int kPadding = 256;
  loadImage(mUrls, &mPixelVec);

  if (mTextureViewDimension == wgpu::TextureViewDimension::Cube) {
    wgpu::TextureDescriptor descriptor;
    descriptor.dimension = mTextureDimension;
    descriptor.size.width = mWidth;
    descriptor.size.height = mHeight;
    descriptor.size.depth = 6;
    descriptor.sampleCount = 1;
    descriptor.format = mFormat;
    descriptor.mipLevelCount = 1;
    descriptor.usage =
        wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::Sampled;
    mTexture = mContext->createTexture(descriptor);

    for (unsigned int i = 0; i < 6; i++) {
      wgpu::BufferDescriptor descriptor;
      descriptor.usage =
          wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::MapWrite;
      descriptor.size = mWidth * mHeight * 4;
      descriptor.mappedAtCreation = true;
      wgpu::Buffer staging = mContext->createBuffer(descriptor);
      memcpy(staging.GetMappedRange(), mPixelVec[i], mWidth * mHeight * 4);
      staging.Unmap();

      wgpu::BufferCopyView bufferCopyView =
          mContext->createBufferCopyView(staging, 0, mWidth * 4, mHeight);
      wgpu::TextureCopyView textureCopyView =
          mContext->createTextureCopyView(mTexture, 0, {0, 0, i});
      wgpu::Extent3D copySize = {static_cast<uint32_t>(mWidth),
                                 static_cast<uint32_t>(mHeight), 1};
      mContext->mCommandBuffers.emplace_back(mContext->copyBufferToTexture(
          bufferCopyView, textureCopyView, copySize));
    }

    wgpu::TextureViewDescriptor viewDescriptor;
    viewDescriptor.nextInChain = nullptr;
    viewDescriptor.dimension = wgpu::TextureViewDimension::Cube;
    viewDescriptor.format = mFormat;
    viewDescriptor.baseMipLevel = 0;
    viewDescriptor.mipLevelCount = 1;
    viewDescriptor.baseArrayLayer = 0;
    viewDescriptor.arrayLayerCount = 6;

    mTextureView = mTexture.CreateView(&viewDescriptor);

    samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
    samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
    samplerDesc.addressModeW = wgpu::AddressMode::ClampToEdge;
    samplerDesc.minFilter = wgpu::FilterMode::Linear;
    samplerDesc.magFilter = wgpu::FilterMode::Linear;
    samplerDesc.mipmapFilter = wgpu::FilterMode::Nearest;

    mSampler = mContext->createSampler(samplerDesc);
  } else  // wgpu::TextureViewDimension::e2D
  {
    int resizedWidth;
    if (mWidth % kPadding == 0) {
      resizedWidth = mWidth;
    } else {
      resizedWidth = (mWidth / 256 + 1) * 256;
    }
    generateMipmap(mPixelVec[0], mWidth, mHeight, 0, mResizedVec, resizedWidth,
                   mHeight, 0, 4, true);

    wgpu::TextureDescriptor descriptor;
    descriptor.dimension = mTextureDimension;
    descriptor.size.width = resizedWidth;
    descriptor.size.height = mHeight;
    descriptor.size.depth = 1;
    descriptor.sampleCount = 1;
    descriptor.format = mFormat;
    descriptor.mipLevelCount =
        static_cast<uint32_t>(std::floor(
            static_cast<float>(std::log2(std::min(mWidth, mHeight))))) +
        1;
    descriptor.usage =
        wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::Sampled;
    mTexture = mContext->createTexture(descriptor);

    int count = 0;
    for (unsigned int i = 0; i < descriptor.mipLevelCount; ++i, ++count) {
      int height = mHeight >> i;
      int width = resizedWidth >> i;
      if (height == 0) {
        height = 1;
      }

      wgpu::BufferDescriptor descriptor;
      descriptor.usage =
          wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::MapWrite;
      descriptor.size = resizedWidth * height * 4;
      descriptor.mappedAtCreation = true;
      wgpu::Buffer staging = mContext->createBuffer(descriptor);
      memcpy(staging.GetMappedRange(), mResizedVec[i],
             resizedWidth * height * 4);
      staging.Unmap();

      wgpu::BufferCopyView bufferCopyView =
          mContext->createBufferCopyView(staging, 0, resizedWidth * 4, height);
      wgpu::TextureCopyView textureCopyView =
          mContext->createTextureCopyView(mTexture, i, {0, 0, 0});
      wgpu::Extent3D copySize = {static_cast<uint32_t>(width),
                                 static_cast<uint32_t>(height), 1};
      mContext->mCommandBuffers.emplace_back(mContext->copyBufferToTexture(
          bufferCopyView, textureCopyView, copySize));
    }

    wgpu::TextureViewDescriptor viewDescriptor;
    viewDescriptor.nextInChain = nullptr;
    viewDescriptor.dimension = wgpu::TextureViewDimension::e2D;
    viewDescriptor.format = mFormat;
    viewDescriptor.baseMipLevel = 0;
    viewDescriptor.mipLevelCount =
        static_cast<uint32_t>(std::floor(
            static_cast<float>(std::log2(std::min(mWidth, mHeight))))) +
        1;
    viewDescriptor.baseArrayLayer = 0;
    viewDescriptor.arrayLayerCount = 1;

    mTextureView = mTexture.CreateView(&viewDescriptor);

    samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
    samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
    samplerDesc.addressModeW = wgpu::AddressMode::ClampToEdge;
    samplerDesc.minFilter = wgpu::FilterMode::Linear;
    samplerDesc.magFilter = wgpu::FilterMode::Linear;

    if (isPowerOf2(mWidth) && isPowerOf2(mHeight)) {
      samplerDesc.mipmapFilter = wgpu::FilterMode::Linear;
    } else {
      samplerDesc.mipmapFilter = wgpu::FilterMode::Nearest;
    }

    mSampler = mContext->createSampler(samplerDesc);
  }

  // TODO(yizhou): check if the pixel destory should delay or fence
}
