#include "PluginProcessor.h"

#include "PluginEditor.h"

TrackPlayerProcessor::TrackPlayerProcessor()
    : AudioProcessor(
          BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)
      ) {
  formatManager.registerBasicFormats();
  // The read-ahead thread lives for the lifetime of the processor — starting
  // and stopping it per track would add latency on every track change and
  // offers no benefit, since there's only ever one reader source attached.
  readAheadThread.startThread();
}

TrackPlayerProcessor::~TrackPlayerProcessor() {
  // Must tear down in source-first order: detach the source from the transport
  // (which also releases its internal BufferingAudioSource pulling on our
  // reader) before we stop the read-ahead thread, otherwise the thread could
  // still be mid-read when we destroy it.
  unloadTransport();
  readAheadThread.stopThread(2000);
}

void TrackPlayerProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
  transport.prepareToPlay(samplesPerBlock, sampleRate);
  // Pre-allocate a stereo scratch sized to the host's block size. We always
  // render the transport into this fixed 2-channel layout and then copy/mix
  // into whatever the output bus is, so channel-count mismatches (e.g. mono
  // file on a stereo bus) have a single uniform code path.
  stereoScratch.setSize(2, samplesPerBlock, false, false, true);
  isPrepared = true;
}

void TrackPlayerProcessor::releaseResources() {
  transport.releaseResources();
  isPrepared = false;
}

bool TrackPlayerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  const auto& out = layouts.getMainOutputChannelSet();
  return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void TrackPlayerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
  const juce::ScopedNoDenormals noDenormals;
  buffer.clear();

  if (!isPrepared) return;

  const int numSamples = buffer.getNumSamples();
  const int numOut = buffer.getNumChannels();

  stereoScratch.clear(0, numSamples);
  const juce::AudioSourceChannelInfo info(&stereoScratch, 0, numSamples);
  // AudioTransportSource short-circuits to silence when it has no source,
  // isn't playing, or has reached EOF, so calling it unconditionally is safe
  // and removes a class of "did I remember to check X?" branches.
  transport.getNextAudioBlock(info);

  if (numOut >= 2) {
    buffer.copyFrom(0, 0, stereoScratch, 0, 0, numSamples);
    buffer.copyFrom(1, 0, stereoScratch, 1, 0, numSamples);
  } else if (numOut == 1) {
    // Mono bus: sum L+R with a 0.5 gain. -6 dB versus summed energy, but
    // prevents clipping on correlated content.
    buffer.copyFrom(0, 0, stereoScratch, 0, 0, numSamples);
    buffer.addFrom(0, 0, stereoScratch, 1, 0, numSamples);
    buffer.applyGain(0.5f);
  }
}

juce::AudioProcessorEditor* TrackPlayerProcessor::createEditor() {
  return new TrackPlayerEditor(*this);
}

// ── Playlist / transport ─────────────────────────────────────────────────────

juce::String TrackPlayerProcessor::getTrackDisplayName(int index) const {
  if (index < 0 || index >= static_cast<int>(playlist.size())) return {};
  return playlist[static_cast<size_t>(index)].displayName;
}

void TrackPlayerProcessor::addTrack(const juce::File& file) {
  if (!file.existsAsFile()) return;

  Track track;
  track.file = file;
  track.displayName = file.getFileName();
  playlist.push_back(std::move(track));

  // First track becomes the current selection so the transport has something
  // loaded and the progress UI has a meaningful length to display. We load
  // stopped — user-driven Play starts audio.
  if (playlist.size() == 1) loadIntoTransport(0, false);
}

void TrackPlayerProcessor::removeTrack(int index) {
  if (index < 0 || index >= static_cast<int>(playlist.size())) return;

  const bool wasCurrent = (index == currentIndex);
  playlist.erase(playlist.begin() + index);

  if (wasCurrent) {
    unloadTransport();
    if (!playlist.empty()) {
      // Prefer the item that slid into the removed slot; fall back to the new
      // last item if we removed the tail.
      const int newIndex = juce::jmin(index, static_cast<int>(playlist.size()) - 1);
      loadIntoTransport(newIndex, false);
    }
  } else if (index < currentIndex) {
    // Entries above the current track shifted down by one; track the move so
    // getCurrentTrackIndex() keeps pointing at the same file.
    --currentIndex;
  }
}

void TrackPlayerProcessor::reorderTrack(int fromIndex, int toIndex) {
  const int size = static_cast<int>(playlist.size());
  if (fromIndex < 0 || fromIndex >= size) return;
  if (toIndex < 0 || toIndex > size) return;
  // Dropping a row either immediately before or after itself is a no-op.
  if (toIndex == fromIndex || toIndex == fromIndex + 1) return;

  // `toIndex` is an insertion point in the *original* list. Once we erase
  // `fromIndex`, anything above it shifts down by one, so the equivalent
  // insertion point in the post-erase list is toIndex-1 when toIndex was
  // past the erased slot.
  const int adjustedTo = (toIndex > fromIndex) ? toIndex - 1 : toIndex;

  Track moved = std::move(playlist[static_cast<size_t>(fromIndex)]);
  playlist.erase(playlist.begin() + fromIndex);
  playlist.insert(playlist.begin() + adjustedTo, std::move(moved));

  // Transport state is unaffected — the underlying source hasn't changed,
  // only its position in the list — but currentIndex needs to track wherever
  // the currently-playing track ended up.
  if (fromIndex == currentIndex) {
    currentIndex = adjustedTo;
  } else {
    if (fromIndex < currentIndex) --currentIndex;
    if (adjustedTo <= currentIndex) ++currentIndex;
  }
}

void TrackPlayerProcessor::selectTrack(int index, bool andPlay) {
  if (index < 0 || index >= static_cast<int>(playlist.size())) return;

  if (index != currentIndex) {
    loadIntoTransport(index, andPlay);
    return;
  }

  if (andPlay && !transport.isPlaying()) transport.start();
}

void TrackPlayerProcessor::playPause() {
  if (currentIndex < 0) return;

  if (transport.isPlaying()) {
    transport.stop();
  } else {
    // Rewind to the top if the previous playthrough ran to EOF, so pressing
    // Play on a finished track restarts it rather than looking like a no-op.
    if (transport.hasStreamFinished()) transport.setPosition(0.0);
    transport.start();
  }
}

void TrackPlayerProcessor::seekSeconds(double seconds) {
  const double length = transport.getLengthInSeconds();
  if (length <= 0.0) return;
  transport.setPosition(juce::jlimit(0.0, length, seconds));
}

void TrackPlayerProcessor::loadIntoTransport(int index, bool andPlay) {
  unloadTransport();

  if (index < 0 || index >= static_cast<int>(playlist.size())) return;

  const auto& track = playlist[static_cast<size_t>(index)];
  std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(track.file));
  if (reader == nullptr) {
    // Decoder failure (unsupported / corrupt / vanished file). Leave the
    // transport empty so the UI shows 0:00 / 0:00 and the user can try
    // another track.
    currentIndex = -1;
    return;
  }

  const double readerSampleRate = reader->sampleRate;
  auto newSource = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);

  // ~0.75 s of read-ahead at 44.1 kHz. Gives the background thread plenty of
  // runway to re-fill the buffer around seeks without burning memory.
  constexpr int kReadAheadSamples = 32768;
  transport.setSource(newSource.get(), kReadAheadSamples, &readAheadThread, readerSampleRate);

  readerSource = std::move(newSource);
  currentIndex = index;

  if (andPlay) transport.start();
}

void TrackPlayerProcessor::unloadTransport() {
  transport.stop();
  // Clear the transport's source before resetting our owned pointer — setSource
  // tears down the BufferingAudioSource wrapping readerSource, guaranteeing no
  // background read can land on a freed reader.
  transport.setSource(nullptr);
  readerSource.reset();
  currentIndex = -1;
}

// ── Persistence ──────────────────────────────────────────────────────────────

void TrackPlayerProcessor::getStateInformation(juce::MemoryBlock& destData) {
  juce::ValueTree state("TrackPlayerState");
  state.setProperty("currentIndex", currentIndex, nullptr);

  juce::ValueTree list("Playlist");
  for (const auto& track : playlist) {
    juce::ValueTree entry("Track");
    entry.setProperty("path", track.file.getFullPathName(), nullptr);
    list.appendChild(entry, nullptr);
  }
  state.appendChild(list, nullptr);

  juce::MemoryOutputStream out(destData, false);
  state.writeToStream(out);
}

void TrackPlayerProcessor::setStateInformation(const void* data, int sizeInBytes) {
  if (sizeInBytes <= 0) return;
  auto state = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeInBytes));
  if (!state.isValid() || !state.hasType("TrackPlayerState")) return;

  unloadTransport();
  playlist.clear();

  auto list = state.getChildWithName("Playlist");
  for (int i = 0; i < list.getNumChildren(); ++i) {
    const juce::File file(list.getChild(i).getProperty("path").toString());
    // Silently drop entries whose files no longer exist — a plugin recall on
    // a machine that's missing some of the original files should still open
    // cleanly with whatever remains.
    if (file.existsAsFile()) {
      playlist.push_back({file, file.getFileName()});
    }
  }

  const int savedIndex = state.getProperty("currentIndex", -1);
  if (savedIndex >= 0 && savedIndex < static_cast<int>(playlist.size())) {
    loadIntoTransport(savedIndex, false);
  } else if (!playlist.empty()) {
    loadIntoTransport(0, false);
  }
}

// Required factory function the JUCE plugin wrapper calls to instantiate the
// processor — one per plugin instance loaded by the host.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TrackPlayerProcessor(); }
