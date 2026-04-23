#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "PlayPauseButton.h"
#include "PlaylistComponents.h"
#include "PluginProcessor.h"

// The editor lives entirely on the message thread. All UI events (clicks,
// drags, file-chooser callbacks) forward into the processor's playlist/
// transport API; the editor never touches audio-thread state directly.
//
// Inherits DragAndDropContainer so the playlist rows can start drags — JUCE
// walks up the component tree from the drag source to find the nearest
// container.
class TrackPlayerEditor final : public juce::AudioProcessorEditor,
                                public juce::ListBoxModel,
                                public juce::DragAndDropContainer,
                                private juce::Timer {
 public:
  explicit TrackPlayerEditor(TrackPlayerProcessor&);
  ~TrackPlayerEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;

  // ── ListBoxModel ──────────────────────────────────────────────────────
  int getNumRows() override;
  // With a custom refreshComponentForRow returning non-null, ListBox never
  // calls paintListBoxItem — but it's a pure virtual on the model so we
  // still have to provide an empty override.
  void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}
  juce::Component* refreshComponentForRow(
      int rowNumber, bool isRowSelected, juce::Component* existingComponentToUpdate
  ) override;

 private:
  void timerCallback() override;
  void refresh();
  void openAddTrackDialog();
  void removeSelectedTrack();
  // Loads `row` into the transport and starts playback; fired by
  // PlaylistRowComponent on double click.
  void playRow(int row);

  // Named to avoid shadowing AudioProcessorEditor::processor (the base class's
  // AudioProcessor& member of the same name). Same object, typed reference.
  TrackPlayerProcessor& player;

  PlaylistListBox playlistBox;
  juce::TextButton addButton{"Add"};
  juce::TextButton removeButton{"Remove"};
  PlayPauseButton playButton;
  juce::Slider progressSlider;
  juce::Label elapsedLabel;
  juce::Label totalLabel;

  // FileChooser is async on macOS — keep it alive as a member so the callback
  // can still reference it when the dialog closes.
  std::unique_ptr<juce::FileChooser> fileChooser;

  // Drag guard. While the user is dragging the progress slider the timer
  // mustn't overwrite the slider value with the transport's current position,
  // or the thumb snaps back under the mouse on every tick.
  bool userDraggingProgress{false};

  // Cached state so the timer only touches the ListBox when something it
  // displays has actually changed (row count, current index, playing flag).
  int lastPlaylistSize{-1};
  int lastCurrentIndex{-1};
  bool lastIsPlaying{false};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackPlayerEditor)
};
