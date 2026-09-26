#include "PluginProcessor.h"
#include "PluginEditor.h"

DaliMixProcessor::DaliMixProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void DaliMixProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    service.setHostSampleRate (sampleRate);
}

bool DaliMixProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void DaliMixProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    HostTime ht;
    bool playing = true;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto t = pos->getTimeInSeconds()) { ht.valid = true; ht.seconds = *t; }
            if (auto p = pos->getPpqPosition())   ht.ppq = *p;
            if (auto b = pos->getBpm())           ht.bpm = *b;
            if (auto ts = pos->getTimeSignature()) { ht.num = ts->numerator; ht.den = ts->denominator; }
        }
    }
    hostIsPlaying.store (playing);

    const int numCh = buffer.getNumChannels();
    const int n = buffer.getNumSamples();
    if (numCh <= 0) return;

    // ---- audition: the plugin plays the stored section while Ableton is stopped
    if (service.isPreviewing())
    {
        if (playing && getPlayHead() != nullptr)
            service.stopPreviewFromAudio();   // Ableton takes over as soon as Play is pressed
        else if (service.renderPreview (buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr,
                                        n, currentSampleRate))
            return;
    }

    // ---- analysis capture (pass-through: the audio is never changed)
    if (! service.isCapturing()) return;
    if (onlyWhilePlaying.load() && ! playing) return;
    const float* l = buffer.getReadPointer (0);
    const float* r = numCh > 1 ? buffer.getReadPointer (1) : l;
    service.pushAudio (l, r, n, ht);
}

juce::AudioProcessorEditor* DaliMixProcessor::createEditor() { return new DaliMixEditor (*this); }

void DaliMixProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("DaliMix");
    state.setProperty ("profile", service.getProfile(), nullptr);
    state.setProperty ("onlyWhilePlaying", onlyWhilePlaying.load(), nullptr);
    juce::MemoryOutputStream mos (destData, false);
    state.writeToStream (mos);
}

void DaliMixProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! state.isValid()) return;
    service.setProfile ((int) state.getProperty ("profile", 0));
    onlyWhilePlaying.store ((bool) state.getProperty ("onlyWhilePlaying", true));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DaliMixProcessor(); }
