#include "PluginEditor.h"

namespace {
// Guards against negatives and NaN/inf — transport length/position can in
// principle return a junk value before a track is loaded.
juce::String formatSeconds(double seconds) {
  if (seconds < 0.0 || !std::isfinite(seconds)) seconds = 0.0;

  const int total = static_cast<int>(std::floor(seconds));
  return juce::String::formatted("%d:%02d", total / 60, total % 60);
}

// Tag prefix for our drag-and-drop source descriptions. A prefix (rather than
// bare integer rows) distinguishes playlist-row drags from anything else that
// might end up as a drag source in the same DragAndDropContainer.
constexpr const char* kDragSourceTag = "playlist-row:";

// Palette tuned toward the macOS dark-player reference: a near-black playlist,
// a slightly raised transport band, saturated blue selection/progress, neutral
// grey controls, and the system-ish green active-track indicator.
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

constexpr int kEditorPadding = 12;
constexpr int kControlsBarH = 56;
constexpr int kControlsRowH = 36;
constexpr int kControlsTopGap = kControlsBarH - kEditorPadding - kControlsRowH;

class TransportSliderLookAndFeel final : public juce::LookAndFeel_V4 {
 public:
  int getSliderThumbRadius(juce::Slider& slider) override {
    const int available = slider.isHorizontal() ? slider.getHeight() : slider.getWidth();
    return juce::jmin(20, static_cast<int>(static_cast<float>(available) * 0.55f));
  }
};

TransportSliderLookAndFeel& transportSliderLookAndFeel() {
  static TransportSliderLookAndFeel lookAndFeel;
  return lookAndFeel;
}

void styleTransportButton(juce::Button& button) {
  button.setColour(juce::TextButton::buttonColourId, kButtonFill);
  button.setColour(juce::TextButton::buttonOnColourId, kButtonFill.brighter(0.06f));
  button.setColour(juce::TextButton::textColourOffId, kButtonText);
  button.setColour(juce::TextButton::textColourOnId, kButtonText);
  button.setColour(juce::ComboBox::outlineColourId, kButtonOutline);
}
}  // namespace

// ── PlayPauseButton ─────────────────────────────────────────────────────────

void PlayPauseButton::paintButton(
    juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown
) {
  // Circular chrome that mirrors LookAndFeel_V4::drawButtonBackground: fill
  // with buttonColourId, stroke with ComboBox::outlineColourId, and tint on
  // hover/press via Colour::contrasting so the visual matches the adjacent
  // Add / Remove rectangles exactly.
  const auto area = getLocalBounds().toFloat();
  const float diameter = juce::jmin(area.getWidth(), area.getHeight());
  // Inset half a pixel so the 1 px stroke lands on pixel boundaries rather
  // than being split across two rows / columns (which would look fuzzy).
  const auto circle =
      juce::Rectangle<float>(diameter - 1.0f, diameter - 1.0f).withCentre(area.getCentre());

  auto fill =
      findColour(juce::TextButton::buttonColourId).withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f);
  if (shouldDrawButtonAsDown)
    fill = fill.contrasting(0.2f);
  else if (shouldDrawButtonAsHighlighted)
    fill = fill.contrasting(0.05f);

  g.setColour(fill);
  g.fillEllipse(circle);

  g.setColour(findColour(juce::ComboBox::outlineColourId));
  g.drawEllipse(circle, 1.0f);

  // Glyph sized to ~40% of the circle's diameter — big enough to read as an
  // icon at a glance, small enough to leave a comfortable ring of empty space
  // inside the circle.
  const float iconH = diameter * 0.4f;
  const float iconW = iconH * 0.9f;

  g.setColour(
      findColour(juce::TextButton::textColourOffId).withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f)
  );

  if (showingPause) {
    // Pause is symmetric, so the bounding box's geometric centre IS the
    // optical centre.
    const auto iconArea = juce::Rectangle<float>(iconW, iconH).withCentre(area.getCentre());
    const float barW = iconArea.getWidth() * 0.35f;
    g.fillRect(iconArea.withWidth(barW));
    g.fillRect(iconArea.withX(iconArea.getRight() - barW).withWidth(barW));
  } else {
    // Right-pointing triangle. The centroid sits at iconW/3 from the base
    // (not the bbox centre), so a geometrically-centred bbox puts the
    // triangle's visual mass left of centre. Shift the bbox right by iconW/6
    // — the bbox-centre-to-centroid offset — so the centroid lands on the
    // circle centre.
    const auto iconArea = juce::Rectangle<float>(iconW, iconH)
                              .withCentre({area.getCentreX() + (iconW / 6.0f), area.getCentreY()});
    juce::Path triangle;
    triangle.addTriangle(
        iconArea.getTopLeft(),
        iconArea.getBottomLeft(),
        {iconArea.getRight(), iconArea.getCentreY()}
    );
    g.fillPath(triangle);
  }
}

// ── PlaylistRowComponent ────────────────────────────────────────────────────

void PlaylistRowComponent::mouseDrag(const juce::MouseEvent& e) {
  if (rowNumber < 0) return;

  auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
  if (container == nullptr || container->isDragAndDropActive()) return;

  // Require a small threshold of movement before flipping into a drag — a
  // mouse jitter during a click shouldn't trigger DnD and eat the normal
  // select/double-click flow.
  if (e.getDistanceFromDragStart() < 5) return;

  const juce::String source = juce::String(kDragSourceTag) + juce::String(rowNumber);
  container->startDragging(source, this);
}

void PlaylistRowComponent::paint(juce::Graphics& g) {
  if (rowNumber < 0 || rowNumber >= processor.getNumTracks()) return;

  const auto rowBackground = getLocalBounds().toFloat().reduced(0.0f, 1.0f);

  if (isSelected) {
    g.setColour(kAccentBlue);
    g.fillRoundedRectangle(rowBackground, 7.0f);
  } else if (hovered) {
    // Subtle lighten-on-hover; distinct from the blue selection tint so the
    // user can tell which row is focused vs. merely under the cursor.
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.fillRoundedRectangle(rowBackground, 7.0f);
  }

  g.setColour(isSelected ? kPrimaryText : kSecondaryText);
  g.setFont(juce::Font(juce::FontOptions(16.0f)));
  g.drawText(
      processor.getTrackDisplayName(rowNumber),
      juce::Rectangle<int>(10, 0, getWidth() - 30, getHeight()),
      juce::Justification::centredLeft,
      true
  );

  // Green dot marks the row currently being played by the transport —
  // independent of selection, so a selected-but-paused row doesn't read the
  // same as one actually running.
  if (rowNumber == processor.getCurrentTrackIndex() && processor.isPlaying()) {
    constexpr float kDiameter = 8.0f;
    const juce::Rectangle<float> dot(
        static_cast<float>(getWidth()) - 18.0f,
        (static_cast<float>(getHeight()) - kDiameter) * 0.5f,
        kDiameter,
        kDiameter
    );
    g.setColour(kPlayingGreen);
    g.fillEllipse(dot);
  }
}

// ── PlaylistListBox ─────────────────────────────────────────────────────────

bool PlaylistListBox::isInterestedInDragSource(const SourceDetails& details) {
  // Only accept the playlist's own row drags — not arbitrary external drops
  // (files from Finder, drags from other components, etc.).
  return details.description.toString().startsWith(kDragSourceTag);
}

void PlaylistListBox::itemDragMove(const SourceDetails& details) {
  const int newRow = insertionIndexForPosition(details.localPosition.x, details.localPosition.y);
  if (newRow == insertionRow) return;
  insertionRow = newRow;
  repaint();
}

void PlaylistListBox::itemDragExit(const SourceDetails&) {
  if (insertionRow < 0) return;
  insertionRow = -1;
  repaint();
}

void PlaylistListBox::itemDropped(const SourceDetails& details) {
  const juce::String source = details.description.toString();
  const int fromIndex = source.fromFirstOccurrenceOf(":", false, false).getIntValue();
  const int toIndex = insertionIndexForPosition(details.localPosition.x, details.localPosition.y);

  // Clear the insertion marker first so any repaint triggered by the reorder
  // callback doesn't briefly show a stale line.
  insertionRow = -1;
  repaint();

  if (onReorder) onReorder(fromIndex, toIndex);
}

void PlaylistListBox::paintOverChildren(juce::Graphics& g) {
  if (insertionRow < 0) return;

  // y = bottom of the row above the insertion point, or 0 if we're above the
  // very first row. getRowPosition handles scroll so the line tracks correctly
  // when the list is scrolled.
  const int y =
      (insertionRow > 0)
          ? getRowPosition(insertionRow - 1, /*relativeToComponentTopLeft=*/true).getBottom()
          : 0;

  g.setColour(kPlayingGreen);
  g.fillRect(0, y - 1, getWidth(), 2);
}

int PlaylistListBox::insertionIndexForPosition(int x, int y) const {
  const int numRows = getListBoxModel() != nullptr ? getListBoxModel()->getNumRows() : 0;
  if (numRows == 0) return 0;

  const int row = getRowContainingPosition(x, y);
  if (row < 0) {
    // Above the first row or below the last — clamp to an end.
    return (y < 0) ? 0 : numRows;
  }

  // Upper half of the row → insert above it; lower half → insert below.
  const auto rowBounds = getRowPosition(row, /*relativeToComponentTopLeft=*/true);
  return (y < rowBounds.getCentreY()) ? row : row + 1;
}

// ── TrackPlayerEditor ───────────────────────────────────────────────────────

TrackPlayerEditor::TrackPlayerEditor(TrackPlayerProcessor& p)
    : AudioProcessorEditor(&p), player(p) {
  setSize(600, 400);
  setResizable(false, false);

  playlistBox.setModel(this);
  playlistBox.setRowHeight(34);
  playlistBox.setMultipleSelectionEnabled(false);
  playlistBox.setColour(juce::ListBox::backgroundColourId, kPlaylistBackground);
  playlistBox.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
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
  styleTransportButton(addButton);
  addAndMakeVisible(addButton);

  removeButton.onClick = [this] { removeSelectedTrack(); };
  styleTransportButton(removeButton);
  addAndMakeVisible(removeButton);

  playButton.onClick = [this] {
    player.playPause();
    // Immediate UI update on click so the button icon flips without waiting
    // for the next timer tick.
    refresh();
  };
  styleTransportButton(playButton);
  addAndMakeVisible(playButton);

  progressSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  progressSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  progressSlider.setColour(juce::Slider::trackColourId, kAccentBlue);
  progressSlider.setColour(juce::Slider::backgroundColourId, kProgressTrack);
  progressSlider.setColour(juce::Slider::thumbColourId, kProgressThumb);
  progressSlider.setLookAndFeel(&transportSliderLookAndFeel());
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
    label.setColour(juce::Label::textColourId, kTimeText);
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
  // 15 Hz is smooth enough for a mm:ss readout and a slider thumb tracking
  // playback, while keeping the editor effectively idle between frames.
  startTimerHz(15);
}

TrackPlayerEditor::~TrackPlayerEditor() {
  progressSlider.setLookAndFeel(nullptr);
  stopTimer();
}

void TrackPlayerEditor::paint(juce::Graphics& g) {
  g.fillAll(kPlaylistBackground);

  auto controlsBand = getLocalBounds();
  controlsBand.removeFromTop(juce::jmax(0, getHeight() - kControlsBarH));
  g.setColour(kControlsBackground);
  g.fillRect(controlsBand);
}

void TrackPlayerEditor::resized() {
  auto bounds = getLocalBounds().reduced(kEditorPadding);

  // Bottom transport row, left→right: play, elapsed, slider, total, Add, Remove.
  auto controls = bounds.removeFromBottom(kControlsRowH);
  bounds.removeFromBottom(kControlsTopGap);
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

int TrackPlayerEditor::getNumRows() { return player.getNumTracks(); }

juce::Component* TrackPlayerEditor::refreshComponentForRow(
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

void TrackPlayerEditor::playRow(int row) {
  if (row < 0 || row >= player.getNumTracks()) return;
  player.selectTrack(row, true);
  // Align the list selection with the just-started track so Remove targets
  // whatever is currently audible by default.
  playlistBox.selectRow(row);
  refresh();
}

// ── Timer / refresh ─────────────────────────────────────────────────────────

void TrackPlayerEditor::timerCallback() {
  // Auto-advance at EOF so a multi-track playlist actually plays through. We
  // do this in the timer rather than a transport change listener because
  // AudioTransportSource stops itself silently on stream-finished.
  if (player.hasTrackEnded() && player.getNumTracks() > 1) {
    const int next = (player.getCurrentTrackIndex() + 1) % player.getNumTracks();
    player.selectTrack(next, true);
  }

  refresh();
}

void TrackPlayerEditor::refresh() {
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

  // Playing state or current-track index changed → the green dot needs to
  // move (or appear / disappear), so force a repaint of visible rows.
  if (currentIndex != lastCurrentIndex || playing != lastIsPlaying) {
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

void TrackPlayerEditor::openAddTrackDialog() {
  fileChooser = std::make_unique<juce::FileChooser>(
      "Add tracks to playlist",
      juce::File::getSpecialLocation(juce::File::userMusicDirectory),
      "*.wav"
  );

  const auto flags = juce::FileBrowserComponent::openMode |
                     juce::FileBrowserComponent::canSelectFiles |
                     juce::FileBrowserComponent::canSelectMultipleItems;

  fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
    const auto files = fc.getResults();
    if (files.isEmpty()) return;

    const int previousCount = player.getNumTracks();
    for (const auto& file : files) player.addTrack(file);

    // Select the first newly-added track — the user probably wants to interact
    // with what they just dropped in (play it, or remove it if misadded), and
    // this also arms the Remove button without an extra click.
    if (player.getNumTracks() > previousCount) playlistBox.selectRow(previousCount);

    refresh();
  });
}

void TrackPlayerEditor::removeSelectedTrack() {
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
