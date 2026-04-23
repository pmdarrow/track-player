#include "PlaylistComponents.h"

#include "TrackPlayerTheme.h"

namespace {

// Tag prefix for our drag-and-drop source descriptions. A prefix, rather than
// bare integer rows, distinguishes playlist-row drags from anything else that
// might end up as a drag source in the same DragAndDropContainer.
constexpr const char* kDragSourceTag = "playlist-row:";

}  // namespace

void PlaylistRowComponent::mouseDrag(const juce::MouseEvent& e) {
  if (rowNumber < 0) return;

  auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
  if (container == nullptr || container->isDragAndDropActive()) return;

  // Require a small threshold of movement before flipping into a drag. Mouse
  // jitter during a click should not eat the normal select/double-click flow.
  if (e.getDistanceFromDragStart() < 5) return;

  const juce::String source = juce::String(kDragSourceTag) + juce::String(rowNumber);
  container->startDragging(source, this);
}

void PlaylistRowComponent::paint(juce::Graphics& g) {
  if (rowNumber < 0 || rowNumber >= processor.getNumTracks()) return;

  const auto rowBackground = getLocalBounds().toFloat().reduced(0.0f, 1.0f);

  if (isSelected) {
    g.setColour(track_player_ui::kAccentBlue);
    g.fillRoundedRectangle(rowBackground, 7.0f);
  } else if (hovered) {
    // Subtle lighten-on-hover; distinct from the blue selection tint so the
    // user can tell which row is focused vs. merely under the cursor.
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.fillRoundedRectangle(rowBackground, 7.0f);
  }

  g.setColour(isSelected ? track_player_ui::kPrimaryText : track_player_ui::kSecondaryText);
  g.setFont(juce::Font(juce::FontOptions(16.0f)));
  g.drawText(
      processor.getTrackDisplayName(rowNumber),
      juce::Rectangle<int>(10, 0, getWidth() - 30, getHeight()),
      juce::Justification::centredLeft,
      true
  );

  // Green dot marks the row currently being played by the transport,
  // independent of selection.
  if (rowNumber == processor.getCurrentTrackIndex() && processor.isPlaying()) {
    constexpr float kDiameter = 8.0f;
    const juce::Rectangle<float> dot(
        static_cast<float>(getWidth()) - 18.0f,
        (static_cast<float>(getHeight()) - kDiameter) * 0.5f,
        kDiameter,
        kDiameter
    );
    g.setColour(track_player_ui::kPlayingGreen);
    g.fillEllipse(dot);
  }
}

bool PlaylistListBox::isInterestedInDragSource(const SourceDetails& details) {
  // Only accept the playlist's own row drags, not arbitrary external drops.
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
  // callback does not briefly show a stale line.
  insertionRow = -1;
  repaint();

  if (onReorder) onReorder(fromIndex, toIndex);
}

void PlaylistListBox::paintOverChildren(juce::Graphics& g) {
  if (insertionRow < 0) return;

  // y = bottom of the row above the insertion point, or 0 if we are above the
  // first row. getRowPosition handles scroll so the line tracks correctly.
  const int y =
      (insertionRow > 0)
          ? getRowPosition(insertionRow - 1, /*relativeToComponentTopLeft=*/true).getBottom()
          : 0;

  g.setColour(track_player_ui::kPlayingGreen);
  g.fillRect(0, y - 1, getWidth(), 2);
}

int PlaylistListBox::insertionIndexForPosition(int x, int y) const {
  const int numRows = getListBoxModel() != nullptr ? getListBoxModel()->getNumRows() : 0;
  if (numRows == 0) return 0;

  const int row = getRowContainingPosition(x, y);
  if (row < 0) {
    // Above the first row or below the last: clamp to an end.
    return (y < 0) ? 0 : numRows;
  }

  // Upper half of the row inserts above it; lower half inserts below.
  const auto rowBounds = getRowPosition(row, /*relativeToComponentTopLeft=*/true);
  return (y < rowBounds.getCentreY()) ? row : row + 1;
}
