// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <stdio.h>

#include <iostream>
#include <sstream>

#include "ppapi/c/pp_errors.h"
#include "ppapi/c/ppb_opengles2.h"
#include "ppapi/cpp/core.h"
#include "ppapi/cpp/fullscreen.h"
#include "ppapi/cpp/graphics_3d.h"
#include "ppapi/cpp/graphics_3d_client.h"
#include "ppapi/cpp/input_event.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/rect.h"
#include "ppapi/cpp/var.h"
#include "base/memory/scoped_ptr.h"
#include "ppapi/proxy/plugin_resource.h"
#include "ppapi/proxy/ppapi_proxy_export.h"
#include "ppapi/shared_impl/resource.h"
#include "ppapi/shared_impl/scoped_pp_resource.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/shared_impl/ppb_graphics_3d_shared.h"
#include "ppapi/lib/gl/gles2/gl2ext_ppapi.h"
#include "ppapi/lib/gl/include/GLES2/gl2.h"
#include "ppapi/lib/gl/include/GLES2/gl2ext.h"

#include "ppapi/c/pp_codecs.h"

#include "ppapi/utility/completion_callback_factory.h"

#include "video_decoder_gstreamer.h"


// Use assert as a poor-man's CHECK, even in non-debug mode.
// Since <assert.h> redefines assert on every inclusion (it doesn't use
// include-guards), make sure this is the last file #include'd in this file.
#undef NDEBUG
#include <assert.h>

// Assert |context_| isn't holding any GL Errors.  Done as a macro instead of a
// function to preserve line number information in the failure message.
#define assertNoGLError() \
  assert(!gles2_if_->GetError(context_->pp_resource()));

using ppapi::thunk::EnterResourceNoLock;
using ppapi::thunk::PPB_Graphics3D_API;


namespace {

struct Shader {
  Shader() : program(0), texcoord_scale_location(0) {}
  ~Shader() {}

  GLuint program;
  GLint texcoord_scale_location;
};

class PPAPIGstreamerInstance : public pp::Instance,
                          public pp::Graphics3DClient {
 public:
  PPAPIGstreamerInstance(PP_Instance instance, pp::Module* module);
  virtual ~PPAPIGstreamerInstance();

  // pp::Instance implementation (see PPP_Instance).
  virtual bool Init(uint32_t argc, const char* argn[], const char* argv[]);

  virtual void DidChangeView(const pp::Rect& position,
                             const pp::Rect& clip_ignored);

  // pp::Graphics3DClient implementation.
  virtual void Graphics3DContextLost() {
    // TODO(jamesr): What's the state of context_? Should we delete the old one
    // or try to revive it somehow?
    // For now, just delete it and construct+bind a new context.
    delete context_;
    context_ = NULL;
    printf("--[CPR] [Graphics3DContextLost]\n");
    pp::CompletionCallback cb = callback_factory_.NewCallback(
        &PPAPIGstreamerInstance::InitGL);
    module_->core()->CallOnMainThread(0, cb, 0);
  }

  virtual bool HandleInputEvent(const pp::InputEvent& event) {
    if (event.GetType() == PP_INPUTEVENT_TYPE_MOUSEUP) {
      fullscreen_ = !fullscreen_;
      pp::Fullscreen(this).SetFullscreen(fullscreen_);
    }
    return true;
  }

   virtual void HandleMessage(const pp::Var& var_message);

//if NO_HOLE
   void PaintPicture(int32_t result);
//#endif //NO_HOLE

 private:

  // GL-related functions.
  void InitGL(int32_t result);
  void FlickerAndPaint(int32_t result);
  bool StartPlay();

  pp::Size plugin_size_;
  pp::Rect windowrect;
  pp::CompletionCallbackFactory<PPAPIGstreamerInstance> callback_factory_;

  // Unowned pointers.
  const PPB_OpenGLES2* gles2_if_;

  pp::Module* module_;


  // Owned data.
  pp::Graphics3D* context_;
  bool fullscreen_;
  void *videodecodergstreamer_;
  std::string src_;

//if NO_HOLE
  ppapi::ScopedPPResource graphics3d_;
  gpu::gles2::GLES2Implementation* gles2_impl_;
  //std::vector<gpu::Mailbox>& mailboxes;

  // Shader program to draw GL_TEXTURE_2D target.
  Shader shader_2d_;

  void Create2DProgramOnce();
  Shader CreateProgram(const char* vertex_shader,
                                 const char* fragment_shader);
  void CreateShader(GLuint program, GLenum type, const char* source, int size);
  void processbuffer(void *buffer, int size);
//#endif //NO_HOLE
};

PPAPIGstreamerInstance::PPAPIGstreamerInstance(PP_Instance instance, pp::Module* module)
    : pp::Instance(instance), pp::Graphics3DClient(this),
      callback_factory_(this),
      gles2_if_(static_cast<const PPB_OpenGLES2*>(
          module->GetBrowserInterface(PPB_OPENGLES2_INTERFACE))),
      module_(module),
      context_(NULL),
      fullscreen_(false),
      videodecodergstreamer_(NULL)
{
  printf("--[CPR] PPAPIGstreamerInstance\n");

  assert(gles2_if_);

//if NO_HOLE
#if 0
  ppapi::thunk::EnterResourceCreationNoLock enter_create(pp_instance());
    if (!enter_create.failed()) {
        int32_t attrib_list[] = {PP_GRAPHICS3DATTRIB_NONE};
    graphics3d_ =
        ppapi::ScopedPPResource(ppapi::ScopedPPResource::PassRef(),
                         enter_create.functions()->CreateGraphics3D(
                             instance, context_->pp_resource(), attrib_list));
    EnterResourceNoLock<PPB_Graphics3D_API> enter_graphics(graphics3d_.get(),
                                                           false);
    ppapi::PPB_Graphics3D_Shared* ppb_graphics3d_shared =
        static_cast<ppapi::PPB_Graphics3D_Shared*>(enter_graphics.object());
    gles2_impl_ = ppb_graphics3d_shared->gles2_impl();

    }
#endif
//#endif //NO_HOLE
  RequestInputEvents(PP_INPUTEVENT_CLASS_MOUSE);
}

PPAPIGstreamerInstance::~PPAPIGstreamerInstance() {
  delete context_;
  VideoDecoderGstreamer_release(videodecodergstreamer_);

}


//--------------------------
//if NO_HOLE
void PPAPIGstreamerInstance::CreateShader(GLuint program,
                              GLenum type,
                              const char* source,
                              int size) {
  GLuint shader = gles2_if_->CreateShader(context_->pp_resource(), type);
  gles2_if_->ShaderSource(context_->pp_resource(), shader, 1, &source, &size);
  gles2_if_->CompileShader(context_->pp_resource(), shader);
  gles2_if_->AttachShader(context_->pp_resource(), program, shader);
  gles2_if_->DeleteShader(context_->pp_resource(), shader);
}

Shader PPAPIGstreamerInstance::CreateProgram(const char* vertex_shader,
                                 const char* fragment_shader) {
  Shader shader;

  // Create shader program.
  shader.program = gles2_if_->CreateProgram(context_->pp_resource());
  CreateShader(
      shader.program, GL_VERTEX_SHADER, vertex_shader, strlen(vertex_shader));
  CreateShader(shader.program,
               GL_FRAGMENT_SHADER,
               fragment_shader,
               strlen(fragment_shader));
  gles2_if_->LinkProgram(context_->pp_resource(), shader.program);
  gles2_if_->UseProgram(context_->pp_resource(), shader.program);
  gles2_if_->Uniform1i(
      context_->pp_resource(),
      gles2_if_->GetUniformLocation(
          context_->pp_resource(), shader.program, "s_texture"),
      0);
  assertNoGLError();

  shader.texcoord_scale_location = gles2_if_->GetUniformLocation(
      context_->pp_resource(), shader.program, "v_scale");

  GLint pos_location = gles2_if_->GetAttribLocation(
      context_->pp_resource(), shader.program, "a_position");
  GLint tc_location = gles2_if_->GetAttribLocation(
      context_->pp_resource(), shader.program, "a_texCoord");
  assertNoGLError();

  gles2_if_->EnableVertexAttribArray(context_->pp_resource(), pos_location);
  gles2_if_->VertexAttribPointer(
      context_->pp_resource(), pos_location, 2, GL_FLOAT, GL_FALSE, 0, 0);
  gles2_if_->EnableVertexAttribArray(context_->pp_resource(), tc_location);
  gles2_if_->VertexAttribPointer(
      context_->pp_resource(),
      tc_location,
      2,
      GL_FLOAT,
      GL_FALSE,
      0,
      static_cast<float*>(0) + 8);  // Skip position coordinates.

  gles2_if_->UseProgram(context_->pp_resource(), 0);
  assertNoGLError();
  return shader;
}

static const char kVertexShader[] =
    "varying vec2 v_texCoord;            \n"
    "attribute vec4 a_position;          \n"
    "attribute vec2 a_texCoord;          \n"
    "uniform vec2 v_scale;               \n"
    "void main()                         \n"
    "{                                   \n"
    "    v_texCoord = v_scale * a_texCoord; \n"
    "    gl_Position = a_position;       \n"
    "}";

void PPAPIGstreamerInstance::Create2DProgramOnce() {
  if (shader_2d_.program)
    return;
  static const char kFragmentShader2D[] =
      "precision mediump float;            \n"
      "varying vec2 v_texCoord;            \n"
      "uniform sampler2D s_texture;        \n"
      "void main()                         \n"
      "{"
      "    gl_FragColor = texture2D(s_texture, v_texCoord); \n"
      "}";
  shader_2d_ = CreateProgram(kVertexShader, kFragmentShader2D);
  assertNoGLError();
}



//----------------------------
void PPAPIGstreamerInstance::processbuffer(void *buffer, int size)
{
    int x = 0;
    int y = 0;
    int half_width = plugin_size_.width() ;
    int half_height = plugin_size_.height() ;
    uint32_t texture;
    PP_VideoPicture *picture = new PP_VideoPicture;
    picture->decode_id = 0;
    picture->texture_target = GL_TEXTURE_2D;
    picture->texture_size = plugin_size_;

#if 0
    //create texture
    gles2_impl_->GenTextures(1, &texture);
    picture->texture_id = texture;
    gles2_impl_->ActiveTexture(GL_TEXTURE0);
    gles2_impl_->BindTexture(GL_TEXTURE_2D, texture);

    gles2_impl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gles2_impl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gles2_impl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gles2_impl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gles2_impl_->TexImage2D(picture->texture_target,
                          0, //level
                          GL_RGBA, //internalformat
                          plugin_size_.width(),
                          plugin_size_.height(),
                          0, //border
                          GL_RGBA, //format
                          GL_UNSIGNED_BYTE, //type
                          buffer); //data

    //gles2_if_->TexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA16F_EXT, meta->width, meta->height, 0,
    //    GL_RGBA, GL_UNSIGNED_BYTE, NULL);
#endif
    gles2_if_->GenTextures(context_->pp_resource(), 1, &texture);
    picture->texture_id = texture;
    gles2_if_->ActiveTexture(context_->pp_resource(), GL_TEXTURE0);
    gles2_if_->BindTexture(context_->pp_resource(), GL_TEXTURE_2D, texture);

    gles2_if_->TexParameteri(context_->pp_resource(), GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gles2_if_->TexParameteri(context_->pp_resource(), GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gles2_if_->TexParameteri(context_->pp_resource(), GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gles2_if_->TexParameteri(context_->pp_resource(), GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gles2_if_->TexImage2D(context_->pp_resource(),
                    picture->texture_target,
                          0, //level
                          GL_RGBA, //internalformat
                          plugin_size_.width(),
                          plugin_size_.height(),
                          0, //border
                          GL_RGBA, //format
                          GL_UNSIGNED_BYTE, //type
                          buffer); //data

    //
    Create2DProgramOnce();
    gles2_if_->UseProgram(context_->pp_resource(), shader_2d_.program);
    gles2_if_->Uniform2f(
        context_->pp_resource(), shader_2d_.texcoord_scale_location, 1.0, 1.0);

    gles2_if_->Viewport(context_->pp_resource(), x, y, half_width, half_height);
    gles2_if_->ActiveTexture(context_->pp_resource(), GL_TEXTURE0);
    gles2_if_->BindTexture(
        context_->pp_resource(), picture->texture_target, picture->texture_id);
    gles2_if_->DrawArrays(context_->pp_resource(), GL_TRIANGLE_STRIP, 0, 4);

    gles2_if_->UseProgram(context_->pp_resource(), 0);


    free(buffer);
}

void PPAPIGstreamerInstance::PaintPicture(int32_t result) {
    int size = 0;
   if (result != 0 || !context_)
       return;
printf("--[CPR] PaintPicture  ---%d\n",__LINE__);


    void *buf = VideoDecoderGstreamer_getBuffer(videodecodergstreamer_, &size);
    if (buf) {
        printf("--[CPR] PaintPicture  buffer present---%d\n",__LINE__);

        //CreateTextures();
        processbuffer(buf, size);
        pp::CompletionCallback cb = callback_factory_.NewCallback(
                &PPAPIGstreamerInstance::PaintPicture);
        context_->SwapBuffers(cb);
        assertNoGLError();
    } else
        PaintPicture(0); //???? A voir retour
}

//#endif //NO_HOLE


bool PPAPIGstreamerInstance::StartPlay()
{
    if(NULL != videodecodergstreamer_) {
        VideoDecoderGstreamer_release(videodecodergstreamer_);
    }

    if("" != src_) {
        videodecodergstreamer_ = VideoDecoderGstreamer_create(true /* hole */);
        if (PP_OK != VideoDecoderGstreamer_initialize(videodecodergstreamer_, src_.c_str()) )
            return false;
        VideoDecoderGstreamer_play(videodecodergstreamer_);

        if( windowrect.width() != 0 && windowrect.height() != 0 ) {
            VideoDecoderGstreamer_setWindow(videodecodergstreamer_,
                            windowrect.x(), windowrect.y(),
                            windowrect.width(), windowrect.height());
        }
        if (!VideoDecoderGstreamer_useHole(videodecodergstreamer_)) {
            PaintPicture(0);
        }
        return true;
    }
    return false;
}

bool PPAPIGstreamerInstance::Init(uint32_t argc, const char* argn[], const char* argv[])
{
    for (uint32_t i = 0; i < argc; i++) {
        printf("-----%s---%s\n",argn[i],argv[i]);
        if (strcmp("src", argn[i]) == 0) {
            src_ = argv[i];
        }
    }
    return StartPlay();
}
void PPAPIGstreamerInstance::DidChangeView(
    const pp::Rect& position, const pp::Rect& clip_ignored)
{
    if (0 == position.width() || 0 == position.height())
        return;
    plugin_size_ = position.size();
    printf("--[CPR] -----PPAPIGstreamerInstance::DidChangeView %d %d %d %d\n",
            position.x(), position.y(), position.width(),position.height());
    // set player video rect
    VideoDecoderGstreamer_setWindow(videodecodergstreamer_,
                    position.x(), position.y(),
                    position.width(), position.height());
    windowrect = position;
    // Initialize graphics.
    InitGL(0);
}
void PPAPIGstreamerInstance::HandleMessage(const pp::Var& var_message)
{
    if (!var_message.is_string())
      return;
    std::string message = var_message.AsString();
    printf("--[CPR] ----HandleMessage %s \n",message.c_str());
    if("playPause()" == message) {
        if(!VideoDecoderGstreamer_isPlaying(videodecodergstreamer_)) {
            StartPlay();
        }
        VideoDecoderGstreamer_pause(videodecodergstreamer_);
    }
    else if ("stop()" == message) {
        VideoDecoderGstreamer_release(videodecodergstreamer_);
    }
}

// This object is the global object representing this plugin library as long
// as it is loaded.
class PPAPIGstreamer : public pp::Module
{
    public:
        PPAPIGstreamer() : pp::Module() {}
        virtual ~PPAPIGstreamer() {}

        virtual pp::Instance* CreateInstance(PP_Instance instance) {
            return new PPAPIGstreamerInstance(instance, this);
        }
};

void PPAPIGstreamerInstance::InitGL(int32_t result)
{
    assert(plugin_size_.width() && plugin_size_.height());

    if (context_) {
        context_->ResizeBuffers(plugin_size_.width(), plugin_size_.height());
        return;
    }
    int32_t context_attributes[] = {
        PP_GRAPHICS3DATTRIB_ALPHA_SIZE, 8,
        PP_GRAPHICS3DATTRIB_BLUE_SIZE, 8,
        PP_GRAPHICS3DATTRIB_GREEN_SIZE, 8,
        PP_GRAPHICS3DATTRIB_RED_SIZE, 8,
        PP_GRAPHICS3DATTRIB_DEPTH_SIZE, 0,
        PP_GRAPHICS3DATTRIB_STENCIL_SIZE, 0,
        PP_GRAPHICS3DATTRIB_SAMPLES, 0,
        PP_GRAPHICS3DATTRIB_SAMPLE_BUFFERS, 0,
        PP_GRAPHICS3DATTRIB_WIDTH, plugin_size_.width(),
        PP_GRAPHICS3DATTRIB_HEIGHT, plugin_size_.height(),
        PP_GRAPHICS3DATTRIB_NONE,
    };
    context_ = new pp::Graphics3D(this, context_attributes);
    assert(!context_->is_null());
#if 1
    // Set viewport window size and clear color bit.
    // Clear color bit.
    gles2_if_->ClearColor(context_->pp_resource(), 0, 1, 0, 1);
    gles2_if_->Clear(context_->pp_resource(), GL_COLOR_BUFFER_BIT);

    gles2_if_->Viewport(context_->pp_resource(), 0, 0, plugin_size_.width(), plugin_size_.height());

#endif
    assert(BindGraphics(*context_));

    assertNoGLError();
printf("--[CPR] InitGL paint %s\n", (VideoDecoderGstreamer_useHole(videodecodergstreamer_)?"true":"false"));
    if (!VideoDecoderGstreamer_useHole(videodecodergstreamer_)) {
//if NO_HOLE
        PaintPicture(0);
//#endif //NO_HOLE
    } else {
        FlickerAndPaint(0);
    }
}

void PPAPIGstreamerInstance::FlickerAndPaint(int32_t result)
{
    if (result != 0 || !context_)
        return;
    //colorkey is red
    if (VideoDecoderGstreamer_useHole(videodecodergstreamer_)) {
        float r = 1;
        float g = 0;
        float b = 0;
        float a = 1;
        gles2_if_->ClearColor(context_->pp_resource(), r, g, b, a);
        gles2_if_->Clear(context_->pp_resource(), GL_COLOR_BUFFER_BIT);
        assertNoGLError();
    }

    pp::CompletionCallback cb = callback_factory_.NewCallback(
        &PPAPIGstreamerInstance::FlickerAndPaint);

    context_->SwapBuffers(cb);
    assertNoGLError();
}

}  // anonymous namespace

namespace pp {
// Factory function for your specialization of the Module object.
Module* CreateModule() {
  return new PPAPIGstreamer();
}
}  // namespace pp
