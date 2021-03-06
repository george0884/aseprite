// Aseprite
// Copyright (C) 2001-2017  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/ui/color_popup.h"

#include "app/app.h"
#include "app/cmd/set_palette.h"
#include "app/color.h"
#include "app/console.h"
#include "app/context.h"
#include "app/context_access.h"
#include "app/document.h"
#include "app/file/palette_file.h"
#include "app/modules/gfx.h"
#include "app/modules/gui.h"
#include "app/modules/palettes.h"
#include "app/resource_finder.h"
#include "app/transaction.h"
#include "app/ui/palette_view.h"
#include "app/ui/skin/skin_theme.h"
#include "app/ui_context.h"
#include "base/bind.h"
#include "base/scoped_value.h"
#include "base/unique_ptr.h"
#include "doc/image_impl.h"
#include "doc/palette.h"
#include "doc/sprite.h"
#include "gfx/border.h"
#include "gfx/size.h"
#include "ui/ui.h"

namespace app {

using namespace ui;
using namespace doc;

enum {
  INDEX_MODE,
  RGB_MODE,
  HSB_MODE,
  GRAY_MODE,
  MASK_MODE
};

static base::UniquePtr<doc::Palette> g_simplePal(nullptr);

class ColorPopup::SimpleColors : public HBox {
public:

  class Item : public Button {
  public:
    Item(ColorPopup* colorPopup, const app::Color& color)
      : Button("")
      , m_colorPopup(colorPopup)
      , m_color(color) {
    }

  private:
    void onClick(Event& ev) override {
      m_colorPopup->setColorWithSignal(m_color);
    }

    void onPaint(PaintEvent& ev) override {
      Graphics* g = ev.graphics();
      skin::SkinTheme* theme = skin::SkinTheme::instance();
      gfx::Rect rc = clientBounds();

      Button::onPaint(ev);

      rc.shrink(theme->calcBorder(this, style()));
      draw_color(g, rc, m_color, doc::ColorMode::RGB);
    }

    ColorPopup* m_colorPopup;
    app::Color m_color;
  };

  SimpleColors(ColorPopup* colorPopup, TooltipManager* tooltips) {
    for (int i=0; i<g_simplePal->size(); ++i) {
      doc::color_t c = g_simplePal->getEntry(i);
      app::Color color =
        app::Color::fromRgb(doc::rgba_getr(c),
                            doc::rgba_getg(c),
                            doc::rgba_getb(c),
                            doc::rgba_geta(c));

      Item* item = new Item(colorPopup, color);
      item->setSizeHint(gfx::Size(16, 16)*ui::guiscale());
      item->setStyle(skin::SkinTheme::instance()->styles.simpleColor());
      addChild(item);

      tooltips->addTooltipFor(
        item, g_simplePal->getEntryName(i), BOTTOM);
    }
  }

  void selectColor(int index) {
    for (int i=0; i<g_simplePal->size(); ++i) {
      children()[i]->setSelected(i == index);
    }
  }

  void deselect() {
    for (int i=0; i<g_simplePal->size(); ++i) {
      children()[i]->setSelected(false);
    }
  }
};

ColorPopup::ColorPopup(const bool canPin,
                       bool showSimpleColors)
  : PopupWindowPin(" ", // Non-empty to create title-bar and close button
                   ClickBehavior::CloseOnClickInOtherWindow,
                   canPin)
  , m_vbox(VERTICAL)
  , m_topBox(HORIZONTAL)
  , m_color(app::Color::fromMask())
  , m_colorPalette(false, PaletteView::SelectOneColor, this, 7*guiscale())
  , m_simpleColors(nullptr)
  , m_colorType(5)
  , m_maskLabel("Transparent Color Selected")
  , m_canPin(canPin)
  , m_disableHexUpdate(false)
{
  if (showSimpleColors) {
    if (!g_simplePal) {
      ResourceFinder rf;
      rf.includeDataDir("palettes/tags.gpl");
      if (rf.findFirst())
        g_simplePal.reset(load_palette(rf.filename().c_str()));
    }

    if (g_simplePal)
      m_simpleColors = new SimpleColors(this, &m_tooltips);
    else
      showSimpleColors = false;
  }

  m_colorType.addItem("Index")->setFocusStop(false);
  m_colorType.addItem("RGB")->setFocusStop(false);
  m_colorType.addItem("HSB")->setFocusStop(false);
  m_colorType.addItem("Gray")->setFocusStop(false);
  m_colorType.addItem("Mask")->setFocusStop(false);

  m_topBox.setBorder(gfx::Border(0));
  m_topBox.setChildSpacing(0);

  m_colorPaletteContainer.attachToView(&m_colorPalette);
  m_colorPaletteContainer.setExpansive(true);
  m_rgbSliders.setExpansive(true);
  m_hsvSliders.setExpansive(true);
  m_graySlider.setExpansive(true);

  m_topBox.addChild(&m_colorType);
  m_topBox.addChild(new Separator("", VERTICAL));
  m_topBox.addChild(&m_hexColorEntry);

  // TODO fix this hack for close button in popup window
  // Move close button (decorative widget) inside the m_topBox
  {
    Widget* closeButton = nullptr;
    WidgetsList decorators;
    for (auto child : children()) {
      if (child->type() == kWindowCloseButtonWidget) {
        closeButton = child;
        removeChild(child);
        break;
      }
    }
    if (closeButton) {
      m_topBox.addChild(new BoxFiller);
      VBox* vbox = new VBox;
      vbox->addChild(closeButton);
      m_topBox.addChild(vbox);
    }
  }
  setText("");                  // To remove title

  m_vbox.addChild(&m_tooltips);
  if (m_simpleColors)
    m_vbox.addChild(m_simpleColors);
  m_vbox.addChild(&m_topBox);
  m_vbox.addChild(&m_colorPaletteContainer);
  m_vbox.addChild(&m_rgbSliders);
  m_vbox.addChild(&m_hsvSliders);
  m_vbox.addChild(&m_graySlider);
  m_vbox.addChild(&m_maskLabel);
  addChild(&m_vbox);

  m_colorType.ItemChange.connect(base::Bind<void>(&ColorPopup::onColorTypeClick, this));

  m_rgbSliders.ColorChange.connect(&ColorPopup::onColorSlidersChange, this);
  m_hsvSliders.ColorChange.connect(&ColorPopup::onColorSlidersChange, this);
  m_graySlider.ColorChange.connect(&ColorPopup::onColorSlidersChange, this);
  m_hexColorEntry.ColorChange.connect(&ColorPopup::onColorHexEntryChange, this);

  // Set RGB just for the sizeHint(), and then deselect the color type
  // (the first setColor() call will setup it correctly.)
  selectColorType(app::Color::RgbType);
  setSizeHint(gfx::Size(300*guiscale(), sizeHint().h));
  m_colorType.deselectItems();

  m_onPaletteChangeConn =
    App::instance()->PaletteChange.connect(&ColorPopup::onPaletteChange, this);

  initTheme();
}

ColorPopup::~ColorPopup()
{
}

void ColorPopup::setColor(const app::Color& color, SetColorOptions options)
{
  m_color = color;

  if (m_simpleColors) {
    int r = color.getRed();
    int g = color.getGreen();
    int b = color.getBlue();
    int a = color.getAlpha();
    int i = g_simplePal->findExactMatch(r, g, b, a, -1);
    if (i >= 0)
      m_simpleColors->selectColor(i);
    else
      m_simpleColors->deselect();
  }

  if (color.getType() == app::Color::IndexType) {
    m_colorPalette.deselect();
    m_colorPalette.selectColor(color.getIndex());
  }

  m_rgbSliders.setColor(m_color);
  m_hsvSliders.setColor(m_color);
  m_graySlider.setColor(m_color);
  if (!m_disableHexUpdate)
    m_hexColorEntry.setColor(m_color);

  if (options == ChangeType)
    selectColorType(m_color.getType());
}

app::Color ColorPopup::getColor() const
{
  return m_color;
}

void ColorPopup::onMakeFloating()
{
  PopupWindowPin::onMakeFloating();

  if (m_canPin) {
    setSizeable(true);
    setMoveable(true);
  }
}

void ColorPopup::onMakeFixed()
{
  PopupWindowPin::onMakeFixed();

  if (m_canPin) {
    setSizeable(false);
    setMoveable(true);
  }
}

void ColorPopup::onPaletteViewIndexChange(int index, ui::MouseButtons buttons)
{
  setColorWithSignal(app::Color::fromIndex(index));
}

void ColorPopup::onColorSlidersChange(ColorSlidersChangeEvent& ev)
{
  setColorWithSignal(ev.color());
  findBestfitIndex(ev.color());
}

void ColorPopup::onColorHexEntryChange(const app::Color& color)
{
  // Disable updating the hex entry so we don't override what the user
  // is writting in the text field.
  m_disableHexUpdate = true;

  setColorWithSignal(color);
  findBestfitIndex(color);

  m_disableHexUpdate = false;
}

void ColorPopup::onSimpleColorClick()
{
  m_colorType.deselectItems();
  if (!g_simplePal)
    return;

  app::Color color = getColor();

  // Find bestfit palette entry
  int r = color.getRed();
  int g = color.getGreen();
  int b = color.getBlue();
  int a = color.getAlpha();

  // Search for the closest color to the RGB values
  int i = g_simplePal->findBestfit(r, g, b, a, 0);
  if (i >= 0) {
    color_t c = g_simplePal->getEntry(i);
    color = app::Color::fromRgb(doc::rgba_getr(c),
                                doc::rgba_getg(c),
                                doc::rgba_getb(c),
                                doc::rgba_geta(c));
  }

  setColorWithSignal(color);
}

void ColorPopup::onColorTypeClick()
{
  if (m_simpleColors)
    m_simpleColors->deselect();

  app::Color newColor = getColor();

  switch (m_colorType.selectedItem()) {
    case INDEX_MODE:
      newColor = app::Color::fromIndex(newColor.getIndex());
      break;
    case RGB_MODE:
      newColor = app::Color::fromRgb(newColor.getRed(),
                                     newColor.getGreen(),
                                     newColor.getBlue(),
                                     newColor.getAlpha());
      break;
    case HSB_MODE:
      newColor = app::Color::fromHsv(newColor.getHue(),
                                     newColor.getSaturation(),
                                     newColor.getValue(),
                                     newColor.getAlpha());
      break;
    case GRAY_MODE:
      newColor = app::Color::fromGray(newColor.getGray(),
                                      newColor.getAlpha());
      break;
    case MASK_MODE:
      newColor = app::Color::fromMask();
      break;
  }

  setColorWithSignal(newColor);
}

void ColorPopup::onPaletteChange()
{
  setColor(getColor(), DoNotChangeType);
  invalidate();
}

void ColorPopup::findBestfitIndex(const app::Color& color)
{
  // Find bestfit palette entry
  int r = color.getRed();
  int g = color.getGreen();
  int b = color.getBlue();
  int a = color.getAlpha();

  // Search for the closest color to the RGB values
  int i = get_current_palette()->findBestfit(r, g, b, a, 0);
  if (i >= 0) {
    m_colorPalette.deselect();
    m_colorPalette.selectColor(i);
  }
}

void ColorPopup::setColorWithSignal(const app::Color& color)
{
  setColor(color, ChangeType);

  // Fire ColorChange signal
  ColorChange(color);
}

void ColorPopup::selectColorType(app::Color::Type type)
{
  m_colorPaletteContainer.setVisible(type == app::Color::IndexType);
  m_rgbSliders.setVisible(type == app::Color::RgbType);
  m_hsvSliders.setVisible(type == app::Color::HsvType);
  m_graySlider.setVisible(type == app::Color::GrayType);
  m_maskLabel.setVisible(type == app::Color::MaskType);

  switch (type) {
    case app::Color::IndexType: m_colorType.setSelectedItem(INDEX_MODE); break;
    case app::Color::RgbType:   m_colorType.setSelectedItem(RGB_MODE); break;
    case app::Color::HsvType:   m_colorType.setSelectedItem(HSB_MODE); break;
    case app::Color::GrayType:  m_colorType.setSelectedItem(GRAY_MODE); break;
    case app::Color::MaskType:  m_colorType.setSelectedItem(MASK_MODE); break;
  }

  // Remove focus from hidden RGB/HSB text entries
  auto widget = manager()->getFocus();
  if (widget && !widget->isVisible()) {
    auto window = widget->window();
    if (window && window == this)
      widget->releaseFocus();
  }

  m_vbox.layout();
  m_vbox.invalidate();
}

} // namespace app
