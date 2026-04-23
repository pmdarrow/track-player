#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Two-state icon button that paints either a right-pointing triangle (play)
// or a pair of vertical bars (pause). Background chrome comes from the host
// look-and-feel via TextButton::buttonColourId, so it matches the adjacent
// Add / Remove buttons visually.
class PlayPauseButton final : public juce::Button {
 public:
  PlayPauseButton() : juce::Button("PlayPause") {}

  void setShowingPause(bool shouldShowPause) {
    if (shouldShowPause == showingPause) return;
    showingPause = shouldShowPause;
    repaint();
  }

 private:
  void paintButton(
      juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown
  ) override;

  bool showingPause{false};
};
