// Aseprite
// Copyright (C) 2001-2015  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_CMD_SET_PIXEL_FORMAT_H_INCLUDED
#define APP_CMD_SET_PIXEL_FORMAT_H_INCLUDED
#pragma once

#include "app/cmd/with_sprite.h"
#include "app/cmd_sequence.h"
#include "doc/pixel_format.h"
#include "render/dithering_algorithm.h"

namespace doc {
  class Sprite;
}

namespace app {
namespace cmd {

  class SetPixelFormat : public Cmd
                       , public WithSprite {
  public:
    SetPixelFormat(doc::Sprite* sprite,
                   const doc::PixelFormat newFormat,
                   const render::DitheringAlgorithm dithering);

  protected:
    void onExecute() override;
    void onUndo() override;
    void onRedo() override;
    size_t onMemSize() const override {
      return sizeof(*this) + m_seq.memSize();
    }

  private:
    void setFormat(PixelFormat format);

    doc::PixelFormat m_oldFormat;
    doc::PixelFormat m_newFormat;
    render::DitheringAlgorithm m_dithering;
    CmdSequence m_seq;
  };

} // namespace cmd
} // namespace app

#endif
