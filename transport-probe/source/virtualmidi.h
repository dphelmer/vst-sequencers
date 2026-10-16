// CoreMIDI virtual source ("MIDI port other apps can receive from"). Lets a Live track take
// MIDI via Ext. In with per-channel choosers, bypassing Live's track-to-track channel merge.
// create/destroy are non-realtime (call from initialize/terminate). noteOn/noteOff/cc are
// called from the audio thread: they stack-allocate a MIDIPacketList and push via MIDIReceived,
// which copies into CoreMIDI's queue (no alloc/locks on our side; accepted practice, but the
// first thing to check in Live if we ever hear xruns).
#pragma once

#include <cstdint>

#if defined(__APPLE__)
#include <CoreMIDI/CoreMIDI.h>
#endif

namespace TransportProbe {

class VirtualMidiOut
{
public:
	VirtualMidiOut () = default;
	~VirtualMidiOut () { destroy (); }
	VirtualMidiOut (const VirtualMidiOut&) = delete;
	VirtualMidiOut& operator= (const VirtualMidiOut&) = delete;

	bool create (const char* name);
	void destroy ();
	bool valid () const;

	void noteOn (int channel, int pitch, int velocity);
	void noteOff (int channel, int pitch);
	void cc (int channel, int number, int value);

private:
	void send3 (uint8_t status, uint8_t d1, uint8_t d2);

#if defined(__APPLE__)
	MIDIClientRef client {0};
	MIDIEndpointRef source {0};
#endif
};

}
