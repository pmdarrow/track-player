#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace simple_audio_player_ui {

// Palette tuned toward the macOS dark-player reference: a near-black playlist,
// a slightly raised transport band, saturated blue selection/progress, neutral
// grey controls, and the system-ish green active-track indicator.
extern const juce::Colour kPlaylistBackground;
extern const juce::Colour kControlsBackground;
extern const juce::Colour kAccentBlue;
extern const juce::Colour kPlayingGreen;
extern const juce::Colour kButtonFill;
extern const juce::Colour kButtonOutline;
extern const juce::Colour kButtonText;
extern const juce::Colour kPrimaryText;
extern const juce::Colour kSecondaryText;
extern const juce::Colour kTimeText;
extern const juce::Colour kProgressTrack;
extern const juce::Colour kProgressThumb;

inline constexpr int kEditorPadding = 12;
inline constexpr int kMinEditorW = 420;
inline constexpr int kMinEditorH = 220;
inline constexpr int kMaxEditorW = 1200;
inline constexpr int kMaxEditorH = 800;
inline constexpr int kControlsBarH = 56;
inline constexpr int kControlsRowH = 36;
inline constexpr int kControlsVerticalMargin = (kControlsBarH - kControlsRowH) / 2;
inline constexpr int kPlaylistTopInset = 0;
inline constexpr int kPlaylistRightInset = 4;

juce::LookAndFeel_V4& transportSliderLookAndFeel();
juce::LookAndFeel_V4& editorLookAndFeel();
juce::LookAndFeel_V4& playlistLookAndFeel();

void styleTransportButton(juce::Button& button);

}  // namespace simple_audio_player_ui
