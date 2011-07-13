// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_PEPPER_PLATFORM_VIDEO_DECODER_IMPL_H_
#define CONTENT_RENDERER_PEPPER_PLATFORM_VIDEO_DECODER_IMPL_H_

#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "media/video/video_decode_accelerator.h"
#include "webkit/plugins/ppapi/plugin_delegate.h"

class GpuChannelHost;
namespace gpu {
class CommandBufferHelper;
}

class PlatformVideoDecoderImpl
    : public webkit::ppapi::PluginDelegate::PlatformVideoDecoder,
      public media::VideoDecodeAccelerator::Client {
 public:
  PlatformVideoDecoderImpl(
      media::VideoDecodeAccelerator::Client* client,
      int32 command_buffer_route_id,
      gpu::CommandBufferHelper* cmd_buffer_helper);
  virtual ~PlatformVideoDecoderImpl();

  // PlatformVideoDecoder implementation.
  virtual bool Initialize(const std::vector<uint32>& configs) OVERRIDE;
  virtual void Decode(
      const media::BitstreamBuffer& bitstream_buffer) OVERRIDE;
  virtual void AssignGLESBuffers(
      const std::vector<media::GLESBuffer>& buffers) OVERRIDE;
  virtual void ReusePictureBuffer(int32 picture_buffer_id);
  virtual void Flush() OVERRIDE;
  virtual void Reset() OVERRIDE;
  virtual void Destroy() OVERRIDE;

  // VideoDecodeAccelerator::Client implementation.
  virtual void ProvidePictureBuffers(
      uint32 requested_num_of_buffers,
      const gfx::Size& dimensions,
      media::VideoDecodeAccelerator::MemoryType type) OVERRIDE;
  virtual void PictureReady(const media::Picture& picture) OVERRIDE;
  virtual void DismissPictureBuffer(int32 picture_buffer_id) OVERRIDE;
  virtual void NotifyInitializeDone() OVERRIDE;
  virtual void NotifyEndOfStream() OVERRIDE;
  virtual void NotifyError(
      media::VideoDecodeAccelerator::Error error) OVERRIDE;
  virtual void NotifyEndOfBitstreamBuffer(int32 bitstream_buffer_id) OVERRIDE;
  virtual void NotifyFlushDone() OVERRIDE;
  virtual void NotifyResetDone() OVERRIDE;
  virtual void NotifyDestroyDone() OVERRIDE;

 private:
  // Client lifetime must exceed lifetime of this class.
  // TODO(vrk/fischman): We should take another look at the overall
  // arcitecture of PPAPI Video Decode to make sure lifetime/ownership makes
  // sense, including lifetime of this client.
  media::VideoDecodeAccelerator::Client* client_;

  // Route ID for the command buffer associated with video decoder's context.
  int32 command_buffer_route_id_;

  // Helper for the command buffer associated with video decoder's context.
  gpu::CommandBufferHelper* cmd_buffer_helper_;

  // Host for GpuVideoDecodeAccelerator.
  // This is owned by the CommandBufferProxy associated with
  // |command_buffer_route_id|.
  media::VideoDecodeAccelerator* decoder_;

  DISALLOW_COPY_AND_ASSIGN(PlatformVideoDecoderImpl);
};
#endif  // CONTENT_RENDERER_PEPPER_PLATFORM_VIDEO_DECODER_IMPL_H_
