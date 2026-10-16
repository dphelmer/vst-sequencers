#include "virtualmidi.h"

#if defined(__APPLE__)

#include <mach/mach_time.h>

namespace TransportProbe {

//------------------------------------------------------------------------
bool VirtualMidiOut::create (const char* name)
{
	if (source)
		return true;

	CFStringRef clientName = CFStringCreateWithCString (nullptr, "TransportProbe",
	                                                    kCFStringEncodingUTF8);
	if (!clientName)
		return false;
	const OSStatus cerr = MIDIClientCreate (clientName, nullptr, nullptr, &client);
	CFRelease (clientName);
	if (cerr != noErr)
	{
		client = 0;
		return false;
	}

	CFStringRef srcName = CFStringCreateWithCString (nullptr, name, kCFStringEncodingUTF8);
	if (!srcName)
	{
		destroy ();
		return false;
	}
	const OSStatus serr = MIDISourceCreate (client, srcName, &source);
	CFRelease (srcName);
	if (serr != noErr)
	{
		source = 0;
		destroy ();
		return false;
	}
	return true;
}

//------------------------------------------------------------------------
void VirtualMidiOut::destroy ()
{
	if (source)
	{
		MIDIEndpointDispose (source);
		source = 0;
	}
	if (client)
	{
		MIDIClientDispose (client);
		client = 0;
	}
}

//------------------------------------------------------------------------
bool VirtualMidiOut::valid () const { return source != 0; }

//------------------------------------------------------------------------
void VirtualMidiOut::send3 (uint8_t status, uint8_t d1, uint8_t d2)
{
	if (!source)
		return;
	// Stack packet list; one MIDIReceived per event. Timestamp = now -> delivered ASAP.
	// Sample accuracy across CoreMIDI is not the goal here (buffer-size jitter); this spike
	// tests channel preservation + arrival, not offset fidelity.
	// MIDIPacketListAdd requires the pointer returned by MIDIPacketListInit (or a previous
	// Add) as curPacket -- passing NULL crashes (Live crash 2026-10-15, MIDIPacketListAdd+160).
	alignas(8) Byte buf[64];
	auto* list = reinterpret_cast<MIDIPacketList*> (buf);
	MIDIPacket* pkt = MIDIPacketListInit (list);
	const Byte data[3] = {status, d1, d2};
	pkt = MIDIPacketListAdd (list, sizeof (buf), pkt, mach_absolute_time (), 3, data);
	if (!pkt)
		return;
	MIDIReceived (source, list);
}

//------------------------------------------------------------------------
void VirtualMidiOut::noteOn (int channel, int pitch, int velocity)
{
	send3 (static_cast<uint8_t> (0x90 | (channel & 0xF)), static_cast<uint8_t> (pitch & 0x7F),
	       static_cast<uint8_t> (velocity & 0x7F));
}

//------------------------------------------------------------------------
void VirtualMidiOut::noteOff (int channel, int pitch)
{
	send3 (static_cast<uint8_t> (0x80 | (channel & 0xF)), static_cast<uint8_t> (pitch & 0x7F), 0);
}

//------------------------------------------------------------------------
void VirtualMidiOut::cc (int channel, int number, int value)
{
	send3 (static_cast<uint8_t> (0xB0 | (channel & 0xF)), static_cast<uint8_t> (number & 0x7F),
	       static_cast<uint8_t> (value & 0x7F));
}

}

#else // !__APPLE__

namespace TransportProbe {

bool VirtualMidiOut::create (const char*) { return false; }
void VirtualMidiOut::destroy () {}
bool VirtualMidiOut::valid () const { return false; }
void VirtualMidiOut::noteOn (int, int, int) {}
void VirtualMidiOut::noteOff (int, int) {}
void VirtualMidiOut::cc (int, int, int) {}
void VirtualMidiOut::send3 (uint8_t, uint8_t, uint8_t) {}

}

#endif
