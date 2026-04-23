#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <utility>

#include "PluginProcessor.h"

// One visual row inside the playlist ListBox. We use a custom row component
// (rather than the default paintListBoxItem callback) so we can track mouse
// hover per-row for the hover highlight, and split single-click (select) from
// double-click (play) behaviour.
class PlaylistRowComponent final : public juce::Component {
 public:
  using PlayAction = std::function<void(int)>;

  PlaylistRowComponent(const TrackPlayerProcessor& processorIn, PlayAction playActionIn)
      : processor(processorIn), onPlay(std::move(playActionIn)) {}

  // Called by TrackPlayerEditor::refreshComponentForRow every time ListBox
  // wants to (re)use this row for a different list index or selection state.
  void setRowInfo(int newRow, bool newSelected) {
    if (newRow == rowNumber && newSelected == isSelected) return;
    rowNumber = newRow;
    isSelected = newSelected;
    repaint();
  }

  void paint(juce::Graphics& g) override;

 private:
  void mouseEnter(const juce::MouseEvent&) override {
    hovered = true;
    repaint();
  }
  void mouseExit(const juce::MouseEvent&) override {
    hovered = false;
    repaint();
  }
  // Single click: just move the list selection. We intentionally don't load
  // or play the track here; that's the double-click behaviour.
  void mouseDown(const juce::MouseEvent& e) override {
    if (rowNumber < 0) return;
    if (auto* list = findParentComponentOfClass<juce::ListBox>())
      list->selectRowsBasedOnModifierKeys(rowNumber, e.mods, false);
  }
  // Double click: load this track into the transport and start playback.
  void mouseDoubleClick(const juce::MouseEvent&) override {
    if (rowNumber >= 0 && onPlay) onPlay(rowNumber);
  }
  // Start a drag-and-drop operation once the mouse has moved far enough from
  // the initial press to distinguish it from a click or double-click.
  void mouseDrag(const juce::MouseEvent& e) override;

  const TrackPlayerProcessor& processor;
  PlayAction onPlay;
  int rowNumber{-1};
  bool isSelected{false};
  bool hovered{false};
};

// Custom ListBox subclass that doubles as a DragAndDropTarget so rows can be
// dropped back onto it to reorder the playlist. Paints an insertion line
// over its rows while a drag is hovering, to show where the drop will land.
class PlaylistListBox final : public juce::ListBox, public juce::DragAndDropTarget {
 public:
  using ReorderAction = std::function<void(int fromIndex, int toIndex)>;

  PlaylistListBox() = default;

  void setReorderAction(ReorderAction action) { onReorder = std::move(action); }

 private:
  bool isInterestedInDragSource(const SourceDetails& details) override;
  void itemDragMove(const SourceDetails& details) override;
  void itemDragExit(const SourceDetails&) override;
  void itemDropped(const SourceDetails& details) override;

  // Paints the insertion line on top of child rows. Regular paint() runs
  // before children are drawn, so the line would be hidden behind them.
  void paintOverChildren(juce::Graphics& g) override;

  // Translates an (x,y) in local coords to an insertion index in
  // [0, numRows]. Returns numRows when the drop is below the last row,
  // 0 when above the first.
  int insertionIndexForPosition(int x, int y) const;

  ReorderAction onReorder;
  // Row index that the insertion marker currently sits above. -1 when no
  // compatible drag is in flight.
  int insertionRow{-1};
};
