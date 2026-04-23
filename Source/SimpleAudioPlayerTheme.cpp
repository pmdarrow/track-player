#include "SimpleAudioPlayerTheme.h"

namespace simple_audio_player_ui {

const juce::Colour kPlaylistBackground{0xff1b1d22};
const juce::Colour kControlsBackground{0xff2b2d33};
const juce::Colour kAccentBlue{0xff0a5fd7};
const juce::Colour kPlayingGreen{0xff32d74b};
const juce::Colour kButtonFill{0xff575b63};
const juce::Colour kButtonOutline{0xff6a6e76};
const juce::Colour kButtonText{0xfff2f3f5};
const juce::Colour kPrimaryText{0xfff4f6f8};
const juce::Colour kSecondaryText{0xffd3d5da};
const juce::Colour kTimeText{0xffd7d9de};
const juce::Colour kProgressTrack{0xff45484f};
const juce::Colour kProgressThumb{0xff9da0a6};

namespace {

class TransportSliderLookAndFeel final : public juce::LookAndFeel_V4 {
 public:
  int getSliderThumbRadius(juce::Slider& slider) override {
    const int available = slider.isHorizontal() ? slider.getHeight() : slider.getWidth();
    return juce::jmin(20, static_cast<int>(static_cast<float>(available) * 0.55f));
  }
};

class EditorLookAndFeel final : public juce::LookAndFeel_V4 {
 public:
  void drawCornerResizer(
      juce::Graphics& g, int width, int height, bool isMouseOver, bool isMouseDragging
  ) override {
    auto handleColour = kButtonOutline.withAlpha(0.58f);
    if (isMouseDragging)
      handleColour = kButtonOutline.withAlpha(0.86f);
    else if (isMouseOver)
      handleColour = kButtonOutline.withAlpha(0.72f);

    g.setColour(handleColour);

    constexpr float kLineThickness = 1.5f;
    for (int i = 0; i < 3; ++i) {
      const float offset = 4.0f + (static_cast<float>(i) * 5.0f);
      g.drawLine(
          static_cast<float>(width) - offset,
          static_cast<float>(height) - 1.5f,
          static_cast<float>(width) - 1.5f,
          static_cast<float>(height) - offset,
          kLineThickness
      );
    }
  }
};

// JUCE exposes scrollbar colours, but not the exact thumb width/placement we
// need here. Override only this paint hook so the playlist can keep a narrow
// right-aligned thumb with breathing room between rows and the scroll affordance.
class PlaylistLookAndFeel final : public juce::LookAndFeel_V4 {
 public:
  int getDefaultScrollbarWidth() override { return 16; }

  void drawScrollbar(
      juce::Graphics& g,
      juce::ScrollBar& scrollbar,
      int x,
      int y,
      int width,
      int height,
      bool isScrollbarVertical,
      int thumbStartPosition,
      int thumbSize,
      bool isMouseOver,
      bool isMouseDown
  ) override {
    if (thumbSize <= 0) return;

    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float availableThickness = isScrollbarVertical ? bounds.getWidth() : bounds.getHeight();
    const float thickness = juce::jmin(8.0f, availableThickness);

    auto thumbColour = kProgressThumb.withAlpha(0.72f);
    if (isMouseDown)
      thumbColour = kProgressThumb;
    else if (isMouseOver || scrollbar.isMouseOver())
      thumbColour = kProgressThumb.withAlpha(0.9f);

    const float inset = juce::jmin(3.0f, static_cast<float>(thumbSize) * 0.25f);
    const float paintedThumbSize = juce::jmax(2.0f, static_cast<float>(thumbSize) - (inset * 2.0f));
    const auto thumbBounds = isScrollbarVertical
                                 ? juce::Rectangle<float>(
                                       bounds.getRight() - thickness,
                                       static_cast<float>(thumbStartPosition) + inset,
                                       thickness,
                                       paintedThumbSize
                                   )
                                 : juce::Rectangle<float>(
                                       static_cast<float>(thumbStartPosition) + inset,
                                       bounds.getBottom() - thickness,
                                       paintedThumbSize,
                                       thickness
                                   );

    g.setColour(thumbColour);
    g.fillRoundedRectangle(thumbBounds, thickness * 0.5f);
  }
};

}  // namespace

juce::LookAndFeel_V4& transportSliderLookAndFeel() {
  static TransportSliderLookAndFeel lookAndFeel;
  return lookAndFeel;
}

juce::LookAndFeel_V4& editorLookAndFeel() {
  static EditorLookAndFeel lookAndFeel;
  return lookAndFeel;
}

juce::LookAndFeel_V4& playlistLookAndFeel() {
  static PlaylistLookAndFeel lookAndFeel;
  return lookAndFeel;
}

void styleTransportButton(juce::Button& button) {
  button.setColour(juce::TextButton::buttonColourId, kButtonFill);
  button.setColour(juce::TextButton::buttonOnColourId, kButtonFill.brighter(0.06f));
  button.setColour(juce::TextButton::textColourOffId, kButtonText);
  button.setColour(juce::TextButton::textColourOnId, kButtonText);
  button.setColour(juce::ComboBox::outlineColourId, kButtonOutline);
}

}  // namespace simple_audio_player_ui
