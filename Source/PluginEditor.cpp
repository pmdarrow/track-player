#include "PluginEditor.h"

#include <cmath>

#include "SimpleAudioPlayerTheme.h"

namespace ui = simple_audio_player_ui;

namespace {
// Guards against negatives and NaN/inf — transport length/position can in
// principle return a junk value before a track is loaded.
juce::String formatSeconds(double seconds) {
  if (seconds < 0.0 || !std::isfinite(seconds)) seconds = 0.0;

  const int total = static_cast<int>(std::floor(seconds));
  return juce::String::formatted("%d:%02d", total / 60, total % 60);
}
}  // namespace

// ── SimpleAudioPlayerEditor ───────────────────────────────────────────────────────

SimpleAudioPlayerEditor::SimpleAudioPlayerEditor(SimpleAudioPlayerProcessor& p)
    : AudioProcessorEditor(&p), player(p) {
  const int editorW = juce::jlimit(ui::kMinEditorW, ui::kMaxEditorW, player.getEditorWidth());
  const int editorH = juce::jlimit(ui::kMinEditorH, ui::kMaxEditorH, player.getEditorHeight());

  setLookAndFeel(&ui::editorLookAndFeel());
  setSize(editorW, editorH);
  setResizeLimits(ui::kMinEditorW, ui::kMinEditorH, ui::kMaxEditorW, ui::kMaxEditorH);
  setResizable(true, true);

  playlistBox.setModel(this);
  playlistBox.setRowHeight(34);
  playlistBox.setMultipleSelectionEnabled(false);
  playlistBox.setColour(juce::ListBox::backgroundColourId, ui::kPlaylistBackground);
  playlistBox.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
  playlistBox.setLookAndFeel(&ui::playlistLookAndFeel());
  if (auto* viewport = playlistBox.getViewport())
    viewport->setScrollBarThickness(ui::playlistLookAndFeel().getDefaultScrollbarWidth());
  playlistBox.setReorderAction([this](int fromIndex, int toIndex) {
    player.reorderTrack(fromIndex, toIndex);
    // reorderTrack's `toIndex` is an insertion point in the original list;
    // the moved track ends up one slot earlier when it was dragged downward.
    const int landedAt = (toIndex > fromIndex) ? toIndex - 1 : toIndex;
    // Keep the list selection on the track the user just dragged so they can
    // chain reorders without re-clicking.
    playlistBox.selectRow(landedAt);
    // Row count is unchanged so our cached-size check won't fire in
    // refresh(); force a repaint so each row picks up its new track name.
    playlistBox.repaint();
    refresh();
  });
  addAndMakeVisible(playlistBox);

  addButton.onClick = [this] { openAddTrackDialog(); };
  ui::styleTransportButton(addButton);
  addAndMakeVisible(addButton);

  removeButton.onClick = [this] { removeSelectedTrack(); };
  ui::styleTransportButton(removeButton);
  addAndMakeVisible(removeButton);

  playButton.onClick = [this] {
    player.playPause();
    // Immediate UI update on click so the button icon flips without waiting
    // for the next timer tick.
    refresh();
  };
  ui::styleTransportButton(playButton);
  addAndMakeVisible(playButton);

  progressSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  progressSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  progressSlider.setColour(juce::Slider::trackColourId, ui::kAccentBlue);
  progressSlider.setColour(juce::Slider::backgroundColourId, ui::kProgressTrack);
  progressSlider.setColour(juce::Slider::thumbColourId, ui::kProgressThumb);
  progressSlider.setLookAndFeel(&ui::transportSliderLookAndFeel());
  // Normalised [0,1] — the slider is track-agnostic; we multiply by the
  // current track length on seek.
  progressSlider.setRange(0.0, 1.0);
  progressSlider.onDragStart = [this] { userDraggingProgress = true; };
  progressSlider.onDragEnd = [this] {
    // Commit the seek only on release — during the drag the thumb moves
    // freely and the audio keeps playing from its previous position, so the
    // user can scrub without hearing a chain of micro-seeks.
    userDraggingProgress = false;
    const double length = player.getLengthSeconds();
    if (length > 0.0) player.seekSeconds(progressSlider.getValue() * length);
  };
  addAndMakeVisible(progressSlider);

  auto initTimeLabel = [this](juce::Label& label, juce::Justification j) {
    label.setJustificationType(j);
    label.setFont(juce::Font(juce::FontOptions(15.0f)));
    label.setColour(juce::Label::textColourId, ui::kTimeText);
    label.setText("0:00", juce::dontSendNotification);
    addAndMakeVisible(label);
  };
  initTimeLabel(elapsedLabel, juce::Justification::centredRight);
  initTimeLabel(totalLabel, juce::Justification::centredLeft);

  // Seed the initial list selection with whatever track the processor has
  // loaded (e.g. from a state-restore). Without this the user sees a populated
  // playlist with no row highlighted, which looks broken.
  if (player.getCurrentTrackIndex() >= 0) playlistBox.selectRow(player.getCurrentTrackIndex());

  refresh();
  // 30 Hz keeps the playing-row indicator smooth while still leaving the
  // editor effectively idle between frames.
  startTimerHz(30);
}

SimpleAudioPlayerEditor::~SimpleAudioPlayerEditor() {
  progressSlider.setLookAndFeel(nullptr);
  playlistBox.setLookAndFeel(nullptr);
  setLookAndFeel(nullptr);
  stopTimer();
}

void SimpleAudioPlayerEditor::paint(juce::Graphics& g) {
  g.fillAll(ui::kPlaylistBackground);

  auto controlsBand = getLocalBounds();
  controlsBand.removeFromTop(juce::jmax(0, getHeight() - ui::kControlsBarH));
  g.setColour(ui::kControlsBackground);
  g.fillRect(controlsBand);
}

void SimpleAudioPlayerEditor::resized() {
  player.setEditorSize(getWidth(), getHeight());

  auto bounds = getLocalBounds();
  auto controlsBand = getLocalBounds();
  controlsBand.removeFromTop(juce::jmax(0, getHeight() - ui::kControlsBarH));

  // Bottom transport row, left→right: play, elapsed, slider, total, Add, Remove.
  auto controls = controlsBand.reduced(ui::kEditorPadding, ui::kControlsVerticalMargin);
  bounds.removeFromBottom(ui::kControlsBarH);
  bounds.removeFromTop(ui::kPlaylistTopInset);
  bounds.removeFromLeft(ui::kEditorPadding);
  bounds.setRight(getWidth() - ui::kPlaylistRightInset);
  playlistBox.setBounds(bounds);

  constexpr int kButtonH = 34;
  constexpr int kAddBtnW = 56;
  constexpr int kRemoveBtnW = 72;
  constexpr int kTimeLabelW = 50;
  constexpr int kGap = 6;
  // Play button is square so its inscribed circle fills the whole frame —
  // no horizontal dead-space around a round button.
  constexpr int kPlayBtnW = kButtonH;

  playButton.setBounds(
      controls.removeFromLeft(kPlayBtnW).withSizeKeepingCentre(kPlayBtnW, kButtonH)
  );
  controls.removeFromLeft(kGap + 4);

  // Pull Add/Remove off the right edge before the slider so the slider gets
  // whatever horizontal space is left over.
  removeButton.setBounds(
      controls.removeFromRight(kRemoveBtnW).withSizeKeepingCentre(kRemoveBtnW, kButtonH)
  );
  controls.removeFromRight(kGap);
  addButton.setBounds(controls.removeFromRight(kAddBtnW).withSizeKeepingCentre(kAddBtnW, kButtonH));
  controls.removeFromRight(kGap * 2);

  elapsedLabel.setBounds(controls.removeFromLeft(kTimeLabelW));
  totalLabel.setBounds(controls.removeFromRight(kTimeLabelW));
  progressSlider.setBounds(controls.reduced(6, 0));
}

// ── ListBoxModel ────────────────────────────────────────────────────────────

int SimpleAudioPlayerEditor::getNumRows() { return player.getNumTracks(); }

juce::Component* SimpleAudioPlayerEditor::refreshComponentForRow(
    int rowNumber, bool isRowSelected, juce::Component* existingComponentToUpdate
) {
  // ListBox owns the row component. On the first call existingComponentToUpdate
  // is null; on subsequent calls it's whatever we previously returned. We
  // dynamic_cast rather than assuming it's ours in case the ListBox somehow
  // hands us a default row component (e.g. after a look-and-feel change).
  auto* row = dynamic_cast<PlaylistRowComponent*>(existingComponentToUpdate);
  if (row == nullptr) {
    delete existingComponentToUpdate;
    row = new PlaylistRowComponent(player, [this](int r) { playRow(r); });
  }
  row->setRowInfo(rowNumber, isRowSelected);
  return row;
}

void SimpleAudioPlayerEditor::playRow(int row) {
  if (row < 0 || row >= player.getNumTracks()) return;
  player.selectTrack(row, true);
  // Align the list selection with the just-started track so Remove targets
  // whatever is currently audible by default.
  playlistBox.selectRow(row);
  refresh();
}

// ── Timer / refresh ─────────────────────────────────────────────────────────

void SimpleAudioPlayerEditor::timerCallback() {
  // Auto-advance at EOF so a multi-track playlist actually plays through. We
  // do this in the timer rather than a transport change listener because
  // AudioTransportSource stops itself silently on stream-finished.
  if (player.hasTrackEnded() && player.getNumTracks() > 1) {
    const int next = (player.getCurrentTrackIndex() + 1) % player.getNumTracks();
    player.selectTrack(next, true);
  }

  refresh();
}

void SimpleAudioPlayerEditor::refresh() {
  const int numTracks = player.getNumTracks();
  const int currentIndex = player.getCurrentTrackIndex();
  const bool playing = player.isPlaying();

  // Row count changes → rebuild the ListBox's row inventory.
  if (numTracks != lastPlaylistSize) {
    lastPlaylistSize = numTracks;
    playlistBox.updateContent();
    // A shrinking playlist can leave the selection pointing off the end. Drop
    // it to -1 rather than silently highlighting a row that no longer exists.
    if (playlistBox.getSelectedRow() >= numTracks) playlistBox.deselectAllRows();
  }

  // Playing state/current-track changes move the indicator; while playing,
  // repaint visible rows at the timer cadence so the small bars animate.
  if (currentIndex != lastCurrentIndex || playing != lastIsPlaying || playing) {
    lastCurrentIndex = currentIndex;
    lastIsPlaying = playing;
    playlistBox.repaint();
  }

  playButton.setShowingPause(playing);
  playButton.setEnabled(currentIndex >= 0);
  removeButton.setEnabled(playlistBox.getSelectedRow() >= 0);

  const double length = player.getLengthSeconds();
  const double position = player.getCurrentPositionSeconds();

  elapsedLabel.setText(formatSeconds(position), juce::dontSendNotification);
  totalLabel.setText(formatSeconds(length), juce::dontSendNotification);

  if (!userDraggingProgress) {
    progressSlider.setValue(length > 0.0 ? position / length : 0.0, juce::dontSendNotification);
  }
  progressSlider.setEnabled(length > 0.0);
}

// ── File add / remove ──────────────────────────────────────────────────────

void SimpleAudioPlayerEditor::openAddTrackDialog() {
  fileChooser = std::make_unique<juce::FileChooser>(
      "Add audio files to playlist",
      juce::File::getSpecialLocation(juce::File::userMusicDirectory),
      player.getSupportedAudioFileWildcard()
  );

  const auto flags = juce::FileBrowserComponent::openMode |
                     juce::FileBrowserComponent::canSelectFiles |
                     juce::FileBrowserComponent::canSelectMultipleItems;

  fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
    const auto files = fc.getResults();
    if (files.isEmpty()) return;

    const int previousCount = player.getNumTracks();
    bool addedAny = false;
    for (const auto& file : files)
      if (player.addTrack(file)) addedAny = true;

    // Select the first newly-added track — the user probably wants to interact
    // with what they just dropped in (play it, or remove it if misadded), and
    // this also arms the Remove button without an extra click.
    if (addedAny) playlistBox.selectRow(previousCount);

    refresh();
  });
}

void SimpleAudioPlayerEditor::removeSelectedTrack() {
  const int selected = playlistBox.getSelectedRow();
  if (selected < 0) return;
  player.removeTrack(selected);

  // Move selection to the row that slid into the removed slot (or the new
  // last row if we removed the tail) so a user can spam Remove without
  // re-clicking between presses.
  const int remaining = player.getNumTracks();
  if (remaining == 0)
    playlistBox.deselectAllRows();
  else
    playlistBox.selectRow(juce::jmin(selected, remaining - 1));

  refresh();
}
