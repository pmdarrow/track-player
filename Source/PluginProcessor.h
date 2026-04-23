#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <memory>
#include <vector>

// AU instrument that plays a user-supplied audio-file playlist.
//
// Threading model:
//   - The UI (message) thread owns the playlist and all transport control calls
//     (add/remove/select/play/seek). AudioTransportSource serialises its own
//     internal state between setSource()/start()/stop() and getNextAudioBlock(),
//     so the audio thread can safely pull samples while the UI mutates.
//   - A dedicated TimeSliceThread does file read-ahead so the audio thread
//     never touches disk inside processBlock().
class SimpleAudioPlayerProcessor final : public juce::AudioProcessor {
 public:
  SimpleAudioPlayerProcessor();
  ~SimpleAudioPlayerProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return "Simple Audio Player"; }
  // AU Music Devices must advertise MIDI input (IS_SYNTH=TRUE). MIDI is accepted
  // but ignored — playback is controlled from the UI.
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String&) override {}

  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  // ── Editor state API ─ message thread writes, host state may read ────────
  int getEditorWidth() const noexcept { return editorWidth.load(); }
  int getEditorHeight() const noexcept { return editorHeight.load(); }
  void setEditorSize(int width, int height) noexcept {
    editorWidth.store(juce::jmax(1, width));
    editorHeight.store(juce::jmax(1, height));
  }

  // ── Playlist / transport API ─ message thread only ──────────────────────
  int getNumTracks() const noexcept { return static_cast<int>(playlist.size()); }
  juce::String getTrackDisplayName(int index) const;
  int getCurrentTrackIndex() const noexcept { return currentIndex; }

  bool addTrack(const juce::File& file);
  juce::String getSupportedAudioFileWildcard() const;
  void removeTrack(int index);
  // Loads `index` into the transport. A no-op if the same track is already
  // loaded and `andPlay` doesn't change the running state.
  void selectTrack(int index, bool andPlay);
  // Moves the track at `fromIndex` to `toIndex` — an *insertion* index in
  // [0, getNumTracks()]. Transport state is preserved: if the moved track is
  // the one currently playing, it keeps playing from the same position.
  void reorderTrack(int fromIndex, int toIndex);

  void playPause();
  bool isPlaying() const noexcept { return transport.isPlaying(); }
  void seekSeconds(double seconds);
  double getCurrentPositionSeconds() const { return transport.getCurrentPosition(); }
  double getLengthSeconds() const { return transport.getLengthInSeconds(); }
  // True once the current track has played through to the end and the
  // transport has stopped itself. Cleared when a new source is loaded or the
  // transport is re-positioned.
  bool hasTrackEnded() const noexcept { return transport.hasStreamFinished(); }

 private:
  struct Track {
    juce::File file;
    juce::String displayName;
  };

  void loadIntoTransport(int index, bool andPlay);
  void unloadTransport();
  bool canOpenAudioFile(const juce::File& file);

  juce::AudioFormatManager formatManager;
  // Background thread powering the read-ahead BufferingAudioSource that
  // AudioTransportSource wraps around our reader source. Keeps disk I/O off the
  // audio thread.
  juce::TimeSliceThread readAheadThread{"Simple Audio Player Read-Ahead"};
  std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
  juce::AudioTransportSource transport;

  std::vector<Track> playlist;
  int currentIndex{-1};

  // Pre-allocated stereo scratch buffer so we can render transport output into
  // a predictable 2-channel layout and then up/downmix to whatever the host
  // bus is. Sized in prepareToPlay so processBlock never allocates.
  juce::AudioBuffer<float> stereoScratch;
  bool isPrepared{false};

  std::atomic<int> editorWidth{600};
  std::atomic<int> editorHeight{400};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleAudioPlayerProcessor)
};
