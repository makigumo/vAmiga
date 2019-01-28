// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the GNU General Public License v3
//
// See https://www.gnu.org for license information
// -----------------------------------------------------------------------------

#ifndef _AUDIO_UNIT_INC
#define _AUDIO_UNIT_INC

#include "HardwareComponent.h"

class AudioUnit : public HardwareComponent {
    
private:
    
    // CPU cycle at the last call to executeUntil()
    uint64_t cycles = 0;
    
    // Time stamp of the last write pointer alignment
    uint64_t lastAlignment = 0;
    
public:
    
    // Number of buffer underflows since power up
    uint64_t bufferUnderflows;
    
    // Number of buffer overflows since power up
    uint64_t bufferOverflows;
    
private:
    
    //
    // Audio ringbuffer
    //
    
    // Number of sound samples stored in ringbuffer
    static constexpr size_t bufferSize = 12288;
    
    /* The audio sample ringbuffer.
     * This ringbuffer serves as the data interface between the emulation code
     * and the audio API (CoreAudio on Mac OS X).
     */
    float ringBuffer[bufferSize];
    
    /* Scaling value for sound samples
     * All sound samples produced by reSID are scaled by this value before they
     * are written into the ringBuffer.
     */
    static constexpr float scale = 0.000005f;
    
    /* Ring buffer read pointer
     */
    uint32_t readPtr;
    
    /* Ring buffer write pointer
     */
    uint32_t writePtr;
    
    /* Current volume
     * A value of 0 or below silences the audio playback.
     */
    int32_t volume;
    
    /* Target volume
     * Whenever an audio sample is written, the volume is increased or
     * decreased by volumeDelta to make it reach the target volume eventually.
     * This feature simulates a fading effect.
     */
    int32_t targetVolume;
    
    /* Maximum volume
     */
    const static int32_t maxVolume = 100000;
    
    /* Volume offset
     * If the current volume does not match the target volume, it is increased
     * or decreased by the specified amount. The increase or decrease takes
     * place whenever an audio sample is generated.
     */
    int32_t volumeDelta;
  
    
    //
    // Constructing and destructing
    //
    
public:
    
    AudioUnit();

    
    //
    // Methods from HardwareComponent
    //
    
private:
    
    void _powerOn() override;
    void _dump() override;
    // void setClockFrequency(uint32_t frequency);
    void didLoadFromBuffer(uint8_t **buffer) override { clearRingbuffer(); }
    
    
    void run();
    void pause();

    
    
    //! @brief    Gathers all values that are displayed in the debugger
    // AudioInfo getInfo();
    
    
    //
    // Configuring the device
    //
    
    // Returns the sample rate.
    uint32_t getSampleRate();
    
    // Sets the samplerate.
    // TODO: Can we adjust the sample rate on the Mac side?
    void setSampleRate(uint32_t sr);
    

    //
    // Volume control
    //
    
    /* Sets the current volume
     */
    void setVolume(int32_t vol) { volume = vol; }
    
    /* Triggers volume ramp up phase
     * Configures volume and targetVolume to simulate a smooth audio fade in
     */
    void rampUp() { targetVolume = maxVolume; volumeDelta = 3; ignoreNextUnderOrOverflow(); }
    void rampUpFromZero() { volume = 0; rampUp(); }
    
    /* Triggers volume ramp down phase
     * Configures volume and targetVolume to simulate a quick audio fade out
     */
    void rampDown() { targetVolume = 0; volumeDelta = 50; ignoreNextUnderOrOverflow(); }
    
    
    //
    // Managing the ringbuffer
    //
    
    // Returns the size of the ringbuffer
    size_t ringbufferSize() { return bufferSize; }
    
    // Returns the position of the read pointer
    uint32_t getReadPtr() { return readPtr; }
    
    // Returns the position of the write pointer
    uint32_t getWritePtr() { return writePtr; }
    
    // Clears the ringbuffer and resets the read and write pointer
    void clearRingbuffer();
    
    // Reads a single audio sample from the ringbuffer
    float readData();
    
    // Reads a single audio sample without moving the read pointer
    float ringbufferData(size_t offset);
    
    /* Reads a certain amount of samples from ringbuffer
     * Samples are stored in a single mono stream
     */
    void readMonoSamples(float *target, size_t n);
    
    /* Reads a certain amount of samples from ringbuffer
     * Samples are stored in two seperate mono streams
     */
    void readStereoSamples(float *target1, float *target2, size_t n);
    
    /* Reads a certain amount of samples from ringbuffer
     * Samples are stored in an interleaved stereo stream
     */
    void readStereoSamplesInterleaved(float *target, size_t n);
    
    /* Writes a certain number of audio samples into ringbuffer
     */
    void writeData(short *data, size_t count);
    
    /* Handles a buffer underflow condition.
     * A buffer underflow occurs when the computer's audio device needs sound
     * samples than SID hasn't produced, yet.
     */
    void handleBufferUnderflow();
    
    /* Handles a buffer overflow condition
     * A buffer overflow occurs when SID is producing more samples than the
     * computer's audio device is able to consume.
     */
    void handleBufferOverflow();
    
    // Signals to ignore the next underflow or overflow condition.
    void ignoreNextUnderOrOverflow() { lastAlignment = mach_absolute_time(); }
    
    // Moves read pointer one position forward
    void advanceReadPtr() { readPtr = (readPtr + 1) % bufferSize; }
    
    // Moves read pointer forward or backward
    void advanceReadPtr(int steps) { readPtr = (readPtr + bufferSize + steps) % bufferSize; }
    
    // Moves the write pointer one position forward.
    void advanceWritePtr() { writePtr = (writePtr + 1) % bufferSize; }
    
    // Moves the write pointer forward or backward.
    void advanceWritePtr(int steps) { writePtr = (writePtr + bufferSize + steps) % bufferSize; }
    
    // Returns number of stored samples in ringbuffer.
    unsigned samplesInBuffer() { return (writePtr + bufferSize - readPtr) % bufferSize; }
    
    // Returns remaining storage capacity of ringbuffer.
    unsigned bufferCapacity() { return (readPtr + bufferSize - writePtr) % bufferSize; }
    
    // Returns the fill level as a percentage value.
    double fillLevel() { return (double)samplesInBuffer() / (double)bufferSize; }
    
    /* Aligns the write pointer.
     * This function puts the write pointer somewhat ahead of the read pointer.
     * With a standard sample rate of 44100 Hz, 735 samples is 1/60 sec.
     */
    const uint32_t samplesAhead = 8 * 735;
    void alignWritePtr() { writePtr = (readPtr  + samplesAhead) % bufferSize; }
    
public:
    
    /* Executes SID until a certain cycle is reached
     */
    void executeUntil(uint64_t targetCycle);
    
    /* Executes SID for a certain number of cycles
     */
    void execute(uint64_t numCycles);
    
    
};


#endif
