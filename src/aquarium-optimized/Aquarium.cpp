//
// Copyright (c) 2019 The Aquarium Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Aquarium.cpp: Create context for specific graphics API.
// Data preparation, load vertex and index buffer, images and shaders.
// Implements logic of rendering background, fishes, seaweeds and
// other models. Calculate fish count for each type of fish.
// Update uniforms for each frame.

#include "Aquarium.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

#include "build/build_config.h"
#include "cxxopts.hpp"
#include "rapidjson/document.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include "ContextFactory.h"
#include "FishModel.h"
#include "Matrix.h"
#include "Program.h"
#include "SeaweedModel.h"
#include "Texture.h"
#include "common/AQUARIUM_ASSERT.h"
#include "common/Path.h"
#include "opengl/ContextGL.h"

#if defined(OS_WIN)
#include <Windows.h>
#endif
#if defined(OS_MAC) || (defined(OS_LINUX) && !defined(OS_CHROMEOS))
#include <ctime>
#endif

Aquarium::Aquarium()
    : mModelEnumMap(),
      mTextureMap(),
      mProgramMap(),
      mAquariumModels(),
      mContext(nullptr),
      mFpsTimer(),
      mCurFishCount(500),
      mPreFishCount(0),
      mTestTime(INT_MAX),
      mFactory(nullptr) {
  g.then = 0.0;
  g.mclock = 0.0;
  g.eyeClock = 0.0;
  g.alpha = "1";

  lightUniforms.lightColor[0] = 1.0f;
  lightUniforms.lightColor[1] = 1.0f;
  lightUniforms.lightColor[2] = 1.0f;
  lightUniforms.lightColor[3] = 1.0f;

  lightUniforms.specular[0] = 1.0f;
  lightUniforms.specular[1] = 1.0f;
  lightUniforms.specular[2] = 1.0f;
  lightUniforms.specular[3] = 1.0f;

  fogUniforms.fogColor[0] = g_fogRed;
  fogUniforms.fogColor[1] = g_fogGreen;
  fogUniforms.fogColor[2] = g_fogBlue;
  fogUniforms.fogColor[3] = 1.0f;

  fogUniforms.fogPower = g_fogPower;
  fogUniforms.fogMult = g_fogMult;
  fogUniforms.fogOffset = g_fogOffset;

  lightUniforms.ambient[0] = g_ambientRed;
  lightUniforms.ambient[1] = g_ambientGreen;
  lightUniforms.ambient[2] = g_ambientBlue;
  lightUniforms.ambient[3] = 0.0f;

  memset(fishCount, 0, 5);
}

Aquarium::~Aquarium() {
  for (auto &tex : mTextureMap) {
    if (tex.second != nullptr) {
      delete tex.second;
      tex.second = nullptr;
    }
  }

  for (auto &program : mProgramMap) {
    if (program.second != nullptr) {
      delete program.second;
      program.second = nullptr;
    }
  }

  for (int i = 0; i < MODELNAME::MODELMAX; ++i) {
    delete mAquariumModels[i];
  }

  if (toggleBitset.test(static_cast<size_t>(TOGGLE::SIMULATINGFISHCOMEANDGO))) {
    while (!mFishBehavior.empty()) {
      Behavior *behave = mFishBehavior.front();
      mFishBehavior.pop();
      delete behave;
    }
  }

  delete mFactory;
}

BACKENDTYPE Aquarium::getBackendType(const std::string &backendPath) {
  if (backendPath == "angle_d3d11") {
#if defined(OS_WIN)
    return (BACKENDTYPE::BACKENDTYPEANGLE | BACKENDTYPE::BACKENDTYPED3D11);
#endif
  } else if (backendPath == "dawn_d3d12") {
#if defined(OS_WIN)
    return (BACKENDTYPE::BACKENDTYPEDAWN | BACKENDTYPE::BACKENDTYPED3D12);
#endif
  } else if (backendPath == "dawn_metal") {
#if defined(OS_MAC)
    return (BACKENDTYPE::BACKENDTYPEDAWN | BACKENDTYPE::BACKENDTYPEMETAL);
#endif
  } else if (backendPath == "dawn_vulkan") {
#if defined(OS_WIN) || (defined(OS_LINUX) && !defined(OS_CHROMEOS))
    return (BACKENDTYPE::BACKENDTYPEDAWN | BACKENDTYPE::BACKENDTYPEVULKAN);
#endif
  } else if (backendPath == "d3d12") {
#if defined(OS_WIN)
    return BACKENDTYPED3D12;
#endif
  } else if (backendPath == "opengl") {
    return BACKENDTYPE::BACKENDTYPEOPENGL;
  }
  return BACKENDTYPENONE;
}

bool Aquarium::init(int argc, char **argv) {
  int windowWidth = 0;
  int windowHeight = 0;

  cxxopts::Options options(argv[0],
                           "A native implementation of WebGL Aquarium");
  cxxopts::OptionAdder oa = options.allow_unrecognised_options().add_options();
  oa("backend", "Set a backend, like 'dawn_d3d12' or 'd3d12'",
     cxxopts::value<std::string>());
  oa("alpha-blending", "Format is <0-1|false>. Set alpha blending",
     cxxopts::value<std::string>());
  oa("buffer-mapping-async",
     "Upload uniforms by buffer mapping async for Dawn backend");
  oa("disable-control-panel", "Turn off control panel");
  oa("disable-d3d12-render-pass",
     "Turn off render pass for dawn_d3d12 and d3d12 backend");
  oa("disable-dawn-validation", "Turn off dawn validation");
  oa("disable-dynamic-buffer-offset",
     "Create many binding groups for a single draw. Dawn only");
  oa("discrete-gpu",
     "Choose discrete gpu to render the application. Dawn and D3D12 only.");
  oa("integrated-gpu",
     "Choose integrated gpu to render the application. Dawn and D3D12 only.");
  oa("enable-full-screen-mode",
     "Render aquarium in full screen mode instead of window mode");
  oa("msaa-sample-count", "Set MSAA sample count. 1 for non-MSAA",
     cxxopts::value<int>());
  oa("num-fish", "Set how many fishes will be rendered.",
     cxxopts::value<int>(mCurFishCount));
  oa("print-log",
     "Print logs including avarage fps when exit the application.");
  oa("simulating-fish-come-and-go",
     "Load fish behavior from FishBehavior.json. Dawn only.");
  oa("test-time", "Render for some seconds then exit.",
     cxxopts::value<int>(mTestTime));
  oa("turn-off-vsync", "Unlimit 60 fps");
  oa("window-size", "Format is <width,height>. Set window size",
     cxxopts::value<std::string>());
  oa("help", "Print help");
  auto result = options.parse(argc, argv);

  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    return false;
  }

  if (!result.count("backend")) {
    std::cout << "Option --backend needs to be designated" << std::endl;
    return false;
  }
  std::string backend = result["backend"].as<std::string>();
  mBackendType = getBackendType(backend);
  if (mBackendType == BACKENDTYPE::BACKENDTYPENONE) {
    std::cout << "Can not create " << backend << " backend" << std::endl;
    return false;
  }
  mFactory = new ContextFactory();
  mContext = mFactory->createContext(mBackendType);
  if (mContext == nullptr) {
    std::cout << "Failed to create context." << std::endl;
    return false;
  }
  std::bitset<static_cast<size_t>(TOGGLE::TOGGLEMAX)> availableToggleBitset =
      mContext->getAvailableToggleBitset();
  if (availableToggleBitset.test(static_cast<size_t>(TOGGLE::DRAWPERMODEL))) {
    toggleBitset.set(static_cast<size_t>(TOGGLE::DRAWPERMODEL));
  }
  if (availableToggleBitset.test(
          static_cast<size_t>(TOGGLE::ENABLEDYNAMICBUFFEROFFSET))) {
    toggleBitset.set(static_cast<size_t>(TOGGLE::ENABLEDYNAMICBUFFEROFFSET));
  }
  toggleBitset.set(static_cast<size_t>(TOGGLE::ENABLEALPHABLENDING));

  if (result.count("alpha-blending")) {
    g.alpha = result["alpha-blending"].as<std::string>();
    if (g.alpha == "false") {
      toggleBitset.reset(static_cast<size_t>(TOGGLE::ENABLEALPHABLENDING));
    }
  }

  if (result.count("buffer-mapping-async")) {
    if (!availableToggleBitset.test(
            static_cast<size_t>(TOGGLE::BUFFERMAPPINGASYNC))) {
      std::cerr << "Buffer mapping async isn't supported for the backend."
                << std::endl;
      return false;
    }

    toggleBitset.set(static_cast<size_t>(TOGGLE::BUFFERMAPPINGASYNC));
  }

  if (result.count("disable-control-panel")) {
    toggleBitset.set(static_cast<size_t>(TOGGLE::DISABLECONTROLPANEL));
  }

  if (result.count("disable-d3d12-render-pass")) {
    if (!availableToggleBitset.test(
            static_cast<size_t>(TOGGLE::DISABLED3D12RENDERPASS))) {
      std::cerr
          << "Render pass is only supported for dawn_d3d12 backend. This "
             "feature "
             "is only supported on Intel gen 10 or more advanced platforms. "
             "Windows 1809 or later version is also required."
          << std::endl;
      return false;
    }
    toggleBitset.set(static_cast<size_t>(TOGGLE::DISABLED3D12RENDERPASS));
  }

  if (result.count("disable-dawn-validation")) {
    if (!availableToggleBitset.test(
            static_cast<size_t>(TOGGLE::DISABLEDAWNVALIDATION))) {
      std::cerr << "Disable validation for Dawn backend." << std::endl;
      return false;
    }
    toggleBitset.set(static_cast<size_t>(TOGGLE::DISABLEDAWNVALIDATION));
  }

  if (result.count("disable-dynamic-buffer-offset")) {
    if (!availableToggleBitset.test(
            static_cast<size_t>(TOGGLE::ENABLEDYNAMICBUFFEROFFSET))) {
      std::cerr
          << "Dynamic buffer offset is only implemented for Dawn Vulkan, Dawn "
             "Metal and D3D12 backend."
          << std::endl;
      return false;
    }
    toggleBitset.set(static_cast<size_t>(TOGGLE::ENABLEDYNAMICBUFFEROFFSET),
                     false);
  }

  if (result.count("discrete-gpu")) {
    if (!availableToggleBitset.test(
            static_cast<size_t>(TOGGLE::INTEGRATEDGPU)) &&
        !availableToggleBitset.test(static_cast<size_t>(TOGGLE::DISCRETEGPU))) {
      std::cerr << "Dynamically choose gpu isn't supported for the backend."
                << std::endl;
      return false;
    }
    if (toggleBitset.test(static_cast<size_t>(TOGGLE::INTEGRATEDGPU))) {
      std::cerr << "Integrated and Discrete gpu cannot be used simultaneosly.";
    }
    toggleBitset.set(static_cast<size_t>(TOGGLE::DISCRETEGPU));
  }

  if (result.count("integrated-gpu")) {
    if (!availableToggleBitset.test(
            static_cast<size_t>(TOGGLE::INTEGRATEDGPU)) &&
        !availableToggleBitset.test(static_cast<size_t>(TOGGLE::DISCRETEGPU))) {
      std::cerr << "Dynamically choose gpu isn't supported for the backend."
                << std::endl;
      return false;
    }
    if (toggleBitset.test(static_cast<size_t>(TOGGLE::DISCRETEGPU))) {
      std::cerr << "Integrated and Discrete gpu cannot be used simultaneosly.";
    }
    toggleBitset.set(static_cast<size_t>(TOGGLE::INTEGRATEDGPU));
  }

  if (result.count("enable-full-screen-mode")) {
    if (!availableToggleBitset.test(
            static_cast<size_t>(TOGGLE::ENABLEFULLSCREENMODE))) {
      std::cerr << "Full screen mode isn't supported for the backend."
                << std::endl;
      return false;
    }

    toggleBitset.set(static_cast<size_t>(TOGGLE::ENABLEFULLSCREENMODE));
  }

  if (result.count("enable-instanced-draws")) {
    /*if
    (!availableToggleBitset.test(static_cast<size_t>(TOGGLE::ENABLEINSTANCEDDRAWS)))
    {
        std::cerr << "Instanced draw path isn't implemented for the backend." <<
    std::endl; return false;
    }
    toggleBitset.set(static_cast<size_t>(TOGGLE::ENABLEINSTANCEDDRAWS));
    // Disable map write aync for instanced draw mode
    toggleBitset.reset(static_cast<size_t>(TOGGLE::BUFFERMAPPINGASYNC));*/
    std::cerr << "Instanced draw path is deprecated." << std::endl;
    return false;
  }

  if (result.count("msaa-sample-count")) {
    mContext->setMSAASampleCount(result["msaa-sample-count"].as<int>());
  }

  if (result.count("print-log")) {
    toggleBitset.set(static_cast<size_t>(TOGGLE::PRINTLOG));
  }

  if (result.count("simulating-fish-come-and-go")) {
    if (!availableToggleBitset.test(
            static_cast<size_t>(TOGGLE::SIMULATINGFISHCOMEANDGO))) {
      std::cerr
          << "Simulating fish come and go is only implemented for Dawn backend."
          << std::endl;
      return false;
    }

    toggleBitset.set(static_cast<size_t>(TOGGLE::SIMULATINGFISHCOMEANDGO));
  }

  if (result.count("test-time")) {
    toggleBitset.set(static_cast<size_t>(TOGGLE::AUTOSTOP));
  }

  if (result.count("turn-off-vsync")) {
    if (!availableToggleBitset.test(
            static_cast<size_t>(TOGGLE::TURNOFFVSYNC))) {
      std::cerr << "Turn off vsync isn't supported for the backend."
                << std::endl;
      return false;
    }

    toggleBitset.set(static_cast<size_t>(TOGGLE::TURNOFFVSYNC));
  }

  if (result.count("window-size")) {
    std::string windowSize = result["window-size"].as<std::string>();
    size_t pos = windowSize.find(",");
    windowWidth = stoi(windowSize.substr(0, pos + 1));
    windowHeight = stoi(windowSize.substr(pos + 1));
    if (windowWidth == 0 || windowHeight == 0) {
      std::cerr << "Please designate window size correctly.";
    }
  }

  if (!mContext->initialize(mBackendType, toggleBitset, windowWidth,
                            windowHeight)) {
    return false;
  }

  calculateFishCount();

  std::cout << "Init resources ..." << std::endl;
  getElapsedTime();

  const ResourceHelper *resourceHelper = mContext->getResourceHelper();
  std::vector<Path> skyUrls;
  resourceHelper->getSkyBoxUrls(&skyUrls);
  mTextureMap["skybox"] = mContext->createTexture("skybox", skyUrls);

  // Init general buffer and binding groups for dawn backend.
  mContext->initGeneralResources(this);
  // Avoid resource allocation in the first render loop
  mPreFishCount = mCurFishCount;

  setupModelEnumMap();
  loadReource();
  mContext->Flush();

  std::cout << "End loading.\nCost " << getElapsedTime() << "s totally."
            << std::endl;
  mContext->showWindow();

  resetFpsTime();

  return true;
}

void Aquarium::resetFpsTime() {
#if defined(OS_WIN)
  g.start = GetTickCount64() / 1000.0;
#elif defined(OS_MAC) || (defined(OS_LINUX) && !defined(OS_CHROMEOS))
  g.start = clock() / 1000000.0;
#else
  ASSERT(false);
#endif
  g.then = g.start;
}

void Aquarium::display() {
  while (!mContext->ShouldQuit()) {
    mContext->KeyBoardQuit();
    render();

    mContext->DoFlush(toggleBitset);

    if (toggleBitset.test(static_cast<size_t>(TOGGLE::AUTOSTOP)) &&
        (g.then - g.start) > mTestTime) {
      break;
    }
  }

  mContext->Terminate();

  if (toggleBitset.test(static_cast<size_t>(TOGGLE::PRINTLOG))) {
    printAvgFps();
  }
}

void Aquarium::loadReource() {
  loadModels();
  loadPlacement();
  if (toggleBitset.test(static_cast<size_t>(TOGGLE::SIMULATINGFISHCOMEANDGO))) {
    loadFishScenario();
  }
}

void Aquarium::setupModelEnumMap() {
  for (auto &info : g_sceneInfo) {
    mModelEnumMap[info.namestr] = info.name;
  }
}

// Load world matrices of models from json file.
void Aquarium::loadPlacement() {
  const ResourceHelper *resourceHelper = mContext->getResourceHelper();
  Path proppath = resourceHelper->getPropPlacementPath();
  std::ifstream PlacementStream(proppath, std::ios::in);
  rapidjson::IStreamWrapper isPlacement(PlacementStream);
  rapidjson::Document document;
  document.ParseStream(isPlacement);

  ASSERT(document.IsObject());

  ASSERT(document.HasMember("objects"));
  const rapidjson::Value &objects = document["objects"];
  ASSERT(objects.IsArray());

  for (rapidjson::SizeType i = 0; i < objects.Size(); ++i) {
    const rapidjson::Value &name = objects[i]["name"];
    const rapidjson::Value &worldMatrix = objects[i]["worldMatrix"];
    ASSERT(worldMatrix.IsArray() && worldMatrix.Size() == 16);

    std::vector<float> matrix;
    for (rapidjson::SizeType j = 0; j < worldMatrix.Size(); ++j) {
      matrix.push_back(worldMatrix[j].GetFloat());
    }

    MODELNAME modelname = mModelEnumMap[name.GetString()];
    mAquariumModels[modelname]->worldmatrices.push_back(matrix);
  }
}

void Aquarium::loadModels() {
  bool enableInstanceddraw =
      toggleBitset.test(static_cast<size_t>(TOGGLE::ENABLEINSTANCEDDRAWS));
  for (const auto &info : g_sceneInfo) {
    if ((enableInstanceddraw && info.type == MODELGROUP::FISH) ||
        ((!enableInstanceddraw) &&
         info.type == MODELGROUP::FISHINSTANCEDDRAW)) {
      continue;
    }
    loadModel(info);
  }
}

void Aquarium::loadFishScenario() {
  const ResourceHelper *resourceHelper = mContext->getResourceHelper();
  Path fishBehaviorPath = resourceHelper->getFishBehaviorPath();

  std::ifstream FishStream(fishBehaviorPath, std::ios::in);
  rapidjson::IStreamWrapper is(FishStream);
  rapidjson::Document document;
  document.ParseStream(is);
  ASSERT(document.IsObject());
  const rapidjson::Value &behaviors = document["behaviors"];
  ASSERT(behaviors.IsArray());

  for (rapidjson::SizeType i = 0; i < behaviors.Size(); ++i) {
    int frame = behaviors[i]["frame"].GetInt();
    std::string op = behaviors[i]["op"].GetString();
    int count = behaviors[i]["count"].GetInt();

    Behavior *behave = new Behavior(frame, op, count);
    mFishBehavior.push(behave);
  }
}

// Load vertex and index buffers, textures and program for each model.
void Aquarium::loadModel(const G_sceneInfo &info) {
  const ResourceHelper *resourceHelper = mContext->getResourceHelper();
  Path imagePath = resourceHelper->getImagePath();
  Path programPath = resourceHelper->getProgramPath();
  Path modelPath = resourceHelper->getModelPath(std::string(info.namestr));

  std::ifstream ModelStream(modelPath, std::ios::in);
  rapidjson::IStreamWrapper is(ModelStream);
  rapidjson::Document document;
  document.ParseStream(is);
  ASSERT(document.IsObject());
  const rapidjson::Value &models = document["models"];
  ASSERT(models.IsArray());

  Model *model;
  if (toggleBitset.test(static_cast<size_t>(TOGGLE::ENABLEALPHABLENDING)) &&
      info.type != MODELGROUP::INNER && info.type != MODELGROUP::OUTSIDE) {
    model = mContext->createModel(this, info.type, info.name, true);
  } else {
    model = mContext->createModel(this, info.type, info.name, info.blend);
  }
  mAquariumModels[info.name] = model;

  auto &value = models.GetArray()[models.GetArray().Size() - 1];
  {
    // set up textures
    const rapidjson::Value &textures = value["textures"];
    for (rapidjson::Value::ConstMemberIterator itr = textures.MemberBegin();
         itr != textures.MemberEnd(); ++itr) {
      std::string name = itr->name.GetString();
      std::string image = itr->value.GetString();

      if (mTextureMap.find(image) == mTextureMap.end()) {
        mTextureMap[image] =
            mContext->createTexture(name, Path(imagePath).push(image));
      }

      model->textureMap[name] = mTextureMap[image];
    }

    // set up vertices
    const rapidjson::Value &arrays = value["fields"];
    for (rapidjson::Value::ConstMemberIterator itr = arrays.MemberBegin();
         itr != arrays.MemberEnd(); ++itr) {
      std::string name = itr->name.GetString();
      int numComponents = itr->value["numComponents"].GetInt();
      std::string type = itr->value["type"].GetString();
      Buffer *buffer;
      if (name == "indices") {
        std::vector<unsigned short> vec;
        for (auto &data : itr->value["data"].GetArray()) {
          vec.push_back(data.GetInt());
        }
        buffer = mContext->createBuffer(numComponents, &vec, true);
      } else {
        std::vector<float> vec;
        for (auto &data : itr->value["data"].GetArray()) {
          vec.push_back(data.GetFloat());
        }
        buffer = mContext->createBuffer(numComponents, &vec, false);
      }

      model->bufferMap[name] = buffer;
    }

    // setup program
    // There are 3 programs
    // DM
    // DM+NM
    // DM+NM+RM
    std::string vsId;
    std::string fsId;

    vsId = info.program[0];
    fsId = info.program[1];

    if (vsId != "" && fsId != "") {
      model->textureMap["skybox"] = mTextureMap["skybox"];
    } else if (model->textureMap["reflection"] != nullptr) {
      vsId = "reflectionMapVertexShader";
      fsId = "reflectionMapFragmentShader";

      model->textureMap["skybox"] = mTextureMap["skybox"];
    } else if (model->textureMap["normalMap"] != nullptr) {
      vsId = "normalMapVertexShader";
      fsId = "normalMapFragmentShader";
    } else {
      vsId = "diffuseVertexShader";
      fsId = "diffuseFragmentShader";
    }

    Program *program;
    if (mProgramMap.find(vsId + fsId) != mProgramMap.end()) {
      program = mProgramMap[vsId + fsId];
    } else {
      program = mContext->createProgram(Path(programPath).push(vsId),
                                        Path(programPath).push(fsId));
      if (toggleBitset.test(static_cast<size_t>(TOGGLE::ENABLEALPHABLENDING)) &&
          info.type != MODELGROUP::INNER && info.type != MODELGROUP::OUTSIDE) {
        program->compileProgram(true, g.alpha);
      } else {
        program->compileProgram(false, g.alpha);
      }
      mProgramMap[vsId + fsId] = program;
    }

    model->setProgram(program);
    model->init();
  }
}

void Aquarium::calculateFishCount() {
  // Calculate fish count for each type of fish
  int numLeft = mCurFishCount;
  for (int i = 0; i < FISHENUM::MAX; ++i) {
    for (auto &fishInfo : fishTable) {
      if (fishInfo.type != i) {
        continue;
      }
      int numfloat = numLeft;
      if (i == FISHENUM::BIG) {
        int temp = mCurFishCount < g_numFishSmall ? 1 : 2;
        numfloat = std::min(numLeft, temp);
      } else if (i == FISHENUM::MEDIUM) {
        if (mCurFishCount < g_numFishMedium) {
          numfloat = std::min(numLeft, mCurFishCount / 10);
        } else if (mCurFishCount < g_numFishBig) {
          numfloat = std::min(numLeft, g_numFishLeftSmall);
        } else {
          numfloat = std::min(numLeft, g_numFishLeftBig);
        }
      }
      numLeft = numLeft - numfloat;
      fishCount[fishInfo.modelName - MODELNAME::MODELSMALLFISHA] = numfloat;
    }
  }
}

double Aquarium::getElapsedTime() {
  // Update our time
#if defined(OS_WIN)
  double now = GetTickCount64() / 1000.0;
#elif defined(OS_MAC) || (defined(OS_LINUX) && !defined(OS_CHROMEOS))
  double now = clock() / 1000000.0;
#else
  double now;
  ASSERT(false);
#endif
  double elapsedTime = 0.0;
  if (g.then == 0.0) {
    elapsedTime = 0.0;
  } else {
    elapsedTime = now - g.then;
  }
  g.then = now;

  return elapsedTime;
}

void Aquarium::printAvgFps() {
  int avg = mFpsTimer.variance();

  std::cout << "Avg FPS: " << avg << std::endl;
  if (avg == 0) {
    std::cout << "Invalid value. The fps is unstable." << std::endl;
  }
}

void Aquarium::updateGlobalUniforms() {
  double elapsedTime = getElapsedTime();
  double renderingTime = g.then - g.start;

  mFpsTimer.update(elapsedTime, renderingTime, mTestTime);
  g.mclock += elapsedTime * g_speed;
  g.eyeClock += elapsedTime * g_eyeSpeed;

  g.eyePosition[0] = sin(g.eyeClock) * g_eyeRadius;
  g.eyePosition[1] = g_eyeHeight;
  g.eyePosition[2] = cos(g.eyeClock) * g_eyeRadius;
  g.target[0] = static_cast<float>(sin(g.eyeClock + M_PI)) * g_targetRadius;
  g.target[1] = g_targetHeight;
  g.target[2] = static_cast<float>(cos(g.eyeClock + M_PI)) * g_targetRadius;

  float nearPlane = 1;
  float farPlane = 25000.0f;
  float aspect = static_cast<float>(mContext->getClientWidth()) /
                 static_cast<float>(mContext->getclientHeight());
  float top =
      tan(matrix::degToRad(g_fieldOfView * g_fovFudge) * 0.5f) * nearPlane;
  float bottom = -top;
  float left = aspect * bottom;
  float right = aspect * top;
  float width = abs(right - left);
  float height = abs(top - bottom);
  float xOff = width * g_net_offset[0] * g_net_offsetMult;
  float yOff = height * g_net_offset[1] * g_net_offsetMult;

  // set frustm and camera look at
  matrix::frustum(g.projection, left + xOff, right + xOff, bottom + yOff,
                  top + yOff, nearPlane, farPlane);
  matrix::cameraLookAt(lightWorldPositionUniform.viewInverse, g.eyePosition,
                       g.target, g.up);
  matrix::inverse4(g.view, lightWorldPositionUniform.viewInverse);
  matrix::mulMatrixMatrix4(lightWorldPositionUniform.viewProjection, g.view,
                           g.projection);
  matrix::inverse4(g.viewProjectionInverse,
                   lightWorldPositionUniform.viewProjection);

  memcpy(g.skyView, g.view, 16 * sizeof(float));
  g.skyView[12] = 0.0;
  g.skyView[13] = 0.0;
  g.skyView[14] = 0.0;
  matrix::mulMatrixMatrix4(g.skyViewProjection, g.skyView, g.projection);
  matrix::inverse4(g.skyViewProjectionInverse, g.skyViewProjection);

  matrix::getAxis(g.v3t0, lightWorldPositionUniform.viewInverse, 0);
  matrix::getAxis(g.v3t1, lightWorldPositionUniform.viewInverse, 1);
  matrix::mulScalarVector(20.0f, g.v3t0, 3);
  matrix::mulScalarVector(30.0f, g.v3t1, 3);
  matrix::addVector(lightWorldPositionUniform.lightWorldPos, g.eyePosition,
                    g.v3t0, 3);
  matrix::addVector(lightWorldPositionUniform.lightWorldPos,
                    lightWorldPositionUniform.lightWorldPos, g.v3t1, 3);

  // update world uniforms for dawn backend
  mContext->updateWorldlUniforms(this);
}

void Aquarium::render() {
  matrix::resetPseudoRandom();

  mContext->preFrame();

  // Global Uniforms should update after command reallocation.
  updateGlobalUniforms();

  if (toggleBitset.test(static_cast<size_t>(TOGGLE::SIMULATINGFISHCOMEANDGO))) {
    if (!mFishBehavior.empty()) {
      Behavior *behave = mFishBehavior.front();
      int frame = behave->getFrame();
      if (frame == 0) {
        mFishBehavior.pop();
        if (behave->getOp() == "+") {
          mCurFishCount += behave->getCount();
        } else {
          mCurFishCount -= behave->getCount();
        }
        std::cout << "Fish count" << mCurFishCount << std::endl;
      } else {
        behave->setFrame(--frame);
      }
    }
  }

  // TODO(yizhou): Functionality of reallocate fish count during rendering
  // isn't supported for instanced draw.
  // To try this functionality now, use composition of "--backend dawn_xxx", or
  // "--backend dawn_xxx --disable-dyanmic-buffer-offset"
  if (!toggleBitset.test(static_cast<size_t>(TOGGLE::ENABLEINSTANCEDDRAWS)))
    if (mCurFishCount != mPreFishCount) {
      calculateFishCount();
      bool enableDynamicBufferOffset = toggleBitset.test(
          static_cast<size_t>(TOGGLE::ENABLEDYNAMICBUFFEROFFSET));
      mContext->reallocResource(mPreFishCount, mCurFishCount,
                                enableDynamicBufferOffset);
      mPreFishCount = mCurFishCount;

      resetFpsTime();
    }

  updateAndDraw();
}

void Aquarium::updateAndDraw() {
  bool drawPerModel =
      toggleBitset.test(static_cast<size_t>(TOGGLE::DRAWPERMODEL));
  int fishBegin =
      toggleBitset.test(static_cast<size_t>(TOGGLE::ENABLEINSTANCEDDRAWS))
          ? MODELNAME::MODELSMALLFISHAINSTANCEDDRAWS
          : MODELNAME::MODELSMALLFISHA;
  int fishEnd =
      toggleBitset.test(static_cast<size_t>(TOGGLE::ENABLEINSTANCEDDRAWS))
          ? MODELNAME::MODELBIGFISHBINSTANCEDDRAWS
          : MODELNAME::MODELBIGFISHB;

  for (int i = MODELRUINCOLUMN; i <= MODELSEAWEEDB; ++i) {
    Model *model = mAquariumModels[i];
    model->prepareForDraw();

    for (auto &world : model->worldmatrices) {
      ASSERT(world.size() == 16);
      memcpy(worldUniforms.world, world.data(), 16 * sizeof(float));
      matrix::mulMatrixMatrix4(worldUniforms.worldViewProjection,
                               worldUniforms.world,
                               lightWorldPositionUniform.viewProjection);
      matrix::inverse4(g.worldInverse, worldUniforms.world);
      matrix::transpose4(worldUniforms.worldInverseTranspose, g.worldInverse);

      model->updatePerInstanceUniforms(worldUniforms);
      if (!drawPerModel) {
        model->draw();
      }
    }
  }

  for (int i = fishBegin; i <= fishEnd; ++i) {
    FishModel *model = static_cast<FishModel *>(mAquariumModels[i]);
    model->prepareForDraw();

    const Fish &fishInfo = fishTable[i - fishBegin];
    int numFish = fishCount[i - fishBegin];
    float fishBaseClock = g.mclock * g_fishSpeed;
    float fishRadius = fishInfo.radius;
    float fishRadiusRange = fishInfo.radiusRange;
    float fishSpeed = fishInfo.speed;
    float fishSpeedRange = fishInfo.speedRange;
    float fishTailSpeed = fishInfo.tailSpeed * g_fishTailSpeed;
    float fishOffset = g_fishOffset;
    // float fishClockSpeed  = g_fishSpeed;
    float fishHeight = g_fishHeight + fishInfo.heightOffset;
    float fishHeightRange = g_fishHeightRange * fishInfo.heightRange;
    float fishXClock = g_fishXClock;
    float fishYClock = g_fishYClock;
    float fishZClock = g_fishZClock;

    for (int ii = 0; ii < numFish; ++ii) {
      float fishClock = fishBaseClock + ii * fishOffset;
      float speed = fishSpeed +
                    static_cast<float>(matrix::pseudoRandom()) * fishSpeedRange;
      float scale = 1.0f + static_cast<float>(matrix::pseudoRandom()) * 1;
      float xRadius = fishRadius + static_cast<float>(matrix::pseudoRandom()) *
                                       fishRadiusRange;
      float yRadius =
          2.0f + static_cast<float>(matrix::pseudoRandom()) * fishHeightRange;
      float zRadius = fishRadius + static_cast<float>(matrix::pseudoRandom()) *
                                       fishRadiusRange;
      float fishSpeedClock = fishClock * speed;
      float xClock = fishSpeedClock * fishXClock;
      float yClock = fishSpeedClock * fishYClock;
      float zClock = fishSpeedClock * fishZClock;

      model->updateFishPerUniforms(
          sin(xClock) * xRadius, sin(yClock) * yRadius + fishHeight,
          cos(zClock) * zRadius, sin(xClock - 0.04f) * xRadius,
          sin(yClock - 0.01f) * yRadius + fishHeight,
          cos(zClock - 0.04f) * zRadius, scale,
          fmod((g.mclock + ii * g_tailOffsetMult) * fishTailSpeed * speed,
               static_cast<float>(M_PI) * 2),
          ii);

      if (!drawPerModel) {
        model->updatePerInstanceUniforms(worldUniforms);
        model->draw();
      }
    }
  }

  mContext->updateFPS(mFpsTimer, &mCurFishCount, &toggleBitset);

  if (drawPerModel) {
    mContext->updateAllFishData();
    mContext->beginRenderPass();
    for (int i = 0; i <= MODELNAME::MODELMAX; ++i) {
      if (i >= MODELNAME::MODELSMALLFISHA && (i < fishBegin || i > fishEnd))
        continue;

      Model *model = mAquariumModels[i];
      model->draw();
    }
    mContext->showFPS();
  }
}
