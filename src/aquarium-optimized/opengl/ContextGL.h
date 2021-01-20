//
// Copyright (c) 2019 The Aquarium Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// ContextGL.h: Defines the accessing to graphics API of OpenGL.

#ifndef CONTEXTGL_H
#define CONTEXTGL_H

#include <vector>

#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"

#ifdef EGL_EGL_PROTOTYPES
#include <memory>

#include "EGL/egl.h"
#include "EGL/eglext.h"
#include "EGL/eglext_angle.h"
#include "EGL/eglplatform.h"
#include "angle_gl.h"
#else
#include "glad/glad.h"
#endif

#include "../Aquarium.h"
#include "../Context.h"

class BufferGL;
class TextureGL;

class ContextGL : public Context {
public:
  static ContextGL *create(BACKENDTYPE backendType);

  ~ContextGL() override;

  bool initialize(
      BACKENDTYPE backend,
      const std::bitset<static_cast<size_t>(TOGGLE::TOGGLEMAX)> &toggleBitset,
      int windowWidth,
      int windowHeight) override;
  void setWindowTitle(const std::string &text) override;
  bool ShouldQuit() override;
  void KeyBoardQuit() override;
  void DoFlush(const std::bitset<static_cast<size_t>(TOGGLE::TOGGLEMAX)>
                   &toggleBitset) override;
  void Terminate() override;
  void showWindow() override;
  void updateFPS(const FPSTimer &fpsTimer,
                 int *fishCount,
                 std::bitset<static_cast<size_t>(TOGGLE::TOGGLEMAX)>
                     *toggleBitset) override;
  void destoryImgUI() override;

  void preFrame() override;
  void enableBlend(bool flag) const;

  Model *createModel(Aquarium *aquarium,
                     MODELGROUP type,
                     MODELNAME name,
                     bool blend) override;
  int getUniformLocation(unsigned int programId, const std::string &name) const;
  int getAttribLocation(unsigned int programId, const std::string &name) const;
  void setUniform(int index, const float *v, int type) const;
  void setTexture(const TextureGL &texture, int index, int unit) const;
  void setAttribs(const BufferGL &bufferGL, int index) const;
  void setIndices(const BufferGL &bufferGL) const;
  void drawElements(const BufferGL &buffer) const;

  Buffer *createBuffer(int numComponents,
                       std::vector<float> *buffer,
                       bool isIndex) override;
  Buffer *createBuffer(int numComponents,
                       std::vector<unsigned short> *buffer,
                       bool isIndex) override;
  unsigned int generateBuffer();
  void deleteBuffer(unsigned int buf);
  void bindBuffer(unsigned int target, unsigned int buf);
  void uploadBuffer(unsigned int target, const std::vector<float> &buf);
  void uploadBuffer(unsigned int target,
                    const std::vector<unsigned short> &buf);

  Program *createProgram(const Path &mVId, const Path &mFId) override;
  unsigned int generateProgram();
  void setProgram(unsigned int program);
  void deleteProgram(unsigned int program);
  bool compileProgram(unsigned int programId,
                      const std::string &VertexShaderCode,
                      const std::string &FragmentShaderCode);
  void bindVAO(unsigned int vao) const;
  unsigned int generateVAO();
  void deleteVAO(unsigned int vao);

  Texture *createTexture(const std::string &name, const Path &url) override;
  Texture *createTexture(const std::string &name,
                         const std::vector<Path> &urls) override;
  unsigned int generateTexture();
  void bindTexture(unsigned int target, unsigned int texture);
  void deleteTexture(unsigned int texture);
  void uploadTexture(unsigned int target,
                     unsigned int format,
                     int width,
                     int height,
                     unsigned char *pixel);
  void setParameter(unsigned int target, unsigned int pname, int param);
  void generateMipmap(unsigned int target);
  void updateAllFishData() override;

protected:
  explicit ContextGL(BACKENDTYPE backendType);

private:
  void initState();
  void initAvailableToggleBitset(BACKENDTYPE backendType) override;
  static void framebufferResizeCallback(GLFWwindow *window,
                                        int width,
                                        int height);

  GLFWwindow *mWindow;
  std::string mGLSLVersion;

#ifdef EGL_EGL_PROTOTYPES
  EGLBoolean FindEGLConfig(EGLDisplay dpy,
                           const EGLint *attrib_list,
                           EGLConfig *config);
  EGLContext createContext(EGLContext share) const;

  EGLSurface mSurface;
  EGLContext mContext;
  EGLDisplay mDisplay;
  EGLConfig mConfig;
#endif
};

#endif  // CONTEXTGL_H
