// Non-realtime logging for the probe. The audio thread pushes fixed-size POD records into
// lock-free SPSC rings; a writer thread formats them to CSV files. Nothing here allocates,
// locks, or touches I/O on the audio thread (except text(), see note).
#pragma once

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

namespace TransportProbe {

enum BlockFlags : uint32_t
{
	kHasContext = 1 << 0,
	kJump = 1 << 1,       // ppq did not match previous block's expected end
	kPlayStart = 1 << 2,  // kPlaying 0 -> 1
	kPlayStop = 1 << 3,   // kPlaying 1 -> 0
	kGenerated = 1 << 4,  // we were allowed to generate (playing + ppq valid + tempo valid)
};

struct BlockRecord
{
	uint64_t seq;
	uint32_t state;         // ProcessContext::state
	uint32_t flags;         // BlockFlags
	int32_t numSamples;
	int32_t processMode;    // 0 realtime, 1 prefetch, 2 offline
	int32_t numInEvents;
	int32_t beatsEmitted;
	int32_t offsEmitted;
	int32_t ccEmitted;
	int32_t tsNum, tsDen;
	int64_t projectTimeSamples;
	int64_t systemTime;
	double sampleRate;
	double ppq;
	double expectedPpq;     // NaN when no previous block to compare to
	double bar, cycleStart, cycleEnd;
	double tempo;
};

struct EventRecord
{
	uint64_t seq;           // block seq this event arrived in
	int32_t type;
	int32_t pitch;
	int32_t sampleOffset;
	int32_t flags;
	double ppqPosition;
};

template <typename T, size_t N>
class SpscRing
{
public:
	SpscRing () : buf (new T[N]) {}
	bool push (const T& v)
	{
		auto h = head.load (std::memory_order_relaxed);
		auto t = tail.load (std::memory_order_acquire);
		if (h - t >= N)
		{
			dropped.fetch_add (1, std::memory_order_relaxed);
			return false;
		}
		buf[h % N] = v;
		head.store (h + 1, std::memory_order_release);
		return true;
	}
	bool pop (T& v)
	{
		auto t = tail.load (std::memory_order_relaxed);
		if (t == head.load (std::memory_order_acquire))
			return false;
		v = buf[t % N];
		tail.store (t + 1, std::memory_order_release);
		return true;
	}
	uint64_t droppedCount () const { return dropped.load (std::memory_order_relaxed); }

private:
	std::unique_ptr<T[]> buf;
	std::atomic<uint64_t> head {0}, tail {0}, dropped {0};
};

class ProbeLog
{
public:
	~ProbeLog () { stop (); }

	// Call from non-realtime thread (setActive(true)). Truncates the files: one instance at a time.
	void start ()
	{
		if (running.load ())
			return;
		const char* home = std::getenv ("HOME");
		dir = std::string (home ? home : "/tmp") + "/Library/Logs/TransportProbe";
		mkdir ((std::string (home ? home : "/tmp") + "/Library/Logs").c_str (), 0755);
		mkdir (dir.c_str (), 0755);
		running = true;
		worker = std::thread ([this] { run (); });
	}

	void stop ()
	{
		if (!running.exchange (false))
			return;
		if (worker.joinable ())
			worker.join ();
	}

	bool push (const BlockRecord& r) { return blocks.push (r); }
	bool push (const EventRecord& r) { return events.push (r); }

	// Lifecycle messages (setActive, setProcessing, setupProcessing...). Uses a mutex held only
	// for a vector swap/append; fine for the rare lifecycle calls, never call from process().
	void text (const char* fmt, ...)
	{
		char line[256];
		va_list ap;
		va_start (ap, fmt);
		vsnprintf (line, sizeof (line), fmt, ap);
		va_end (ap);
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (
		              std::chrono::system_clock::now ().time_since_epoch ())
		              .count ();
		char stamped[320];
		snprintf (stamped, sizeof (stamped), "%lld %s\n", static_cast<long long> (ms), line);
		std::lock_guard<std::mutex> g (textMutex);
		pendingText.emplace_back (stamped);
	}

private:
	void run ()
	{
		FILE* fb = std::fopen ((dir + "/blocks.csv").c_str (), "w");
		FILE* fe = std::fopen ((dir + "/events.csv").c_str (), "w");
		FILE* ft = std::fopen ((dir + "/lifecycle.log").c_str (), "w");
		if (fb)
			std::fputs ("seq,state,flags,numSamples,processMode,numInEvents,beatsEmitted,offsEmitted,"
			            "ccEmitted,tsNum,tsDen,projectTimeSamples,systemTime,sampleRate,ppq,"
			            "expectedPpq,bar,cycleStart,cycleEnd,tempo\n",
			            fb);
		if (fe)
			std::fputs ("blockSeq,type,pitch,sampleOffset,flags,ppqPosition\n", fe);

		auto drain = [&] {
			BlockRecord b;
			while (blocks.pop (b))
				if (fb)
					std::fprintf (fb,
					              "%llu,%u,%u,%d,%d,%d,%d,%d,%d,%d,%d,%lld,%lld,%.1f,%.6f,%.6f,%.6f,%.6f,%.6f,%.4f\n",
					              (unsigned long long)b.seq, b.state, b.flags, b.numSamples,
					              b.processMode, b.numInEvents, b.beatsEmitted, b.offsEmitted,
					              b.ccEmitted, b.tsNum, b.tsDen, (long long)b.projectTimeSamples,
					              (long long)b.systemTime, b.sampleRate, b.ppq, b.expectedPpq, b.bar,
					              b.cycleStart, b.cycleEnd, b.tempo);
			EventRecord e;
			while (events.pop (e))
				if (fe)
					std::fprintf (fe, "%llu,%d,%d,%d,%d,%.6f\n", (unsigned long long)e.seq, e.type,
					              e.pitch, e.sampleOffset, e.flags, e.ppqPosition);
			std::vector<std::string> t;
			{
				std::lock_guard<std::mutex> g (textMutex);
				t.swap (pendingText);
			}
			for (auto& s : t)
				if (ft)
					std::fputs (s.c_str (), ft);
			if (fb) std::fflush (fb);
			if (fe) std::fflush (fe);
			if (ft) std::fflush (ft);
		};

		while (running.load ())
		{
			drain ();
			std::this_thread::sleep_for (std::chrono::milliseconds (20));
		}
		drain ();
		if (ft)
			std::fprintf (ft, "dropped block records: %llu, event records: %llu\n",
			              (unsigned long long)blocks.droppedCount (),
			              (unsigned long long)events.droppedCount ());
		if (fb) std::fclose (fb);
		if (fe) std::fclose (fe);
		if (ft) std::fclose (ft);
	}

	SpscRing<BlockRecord, (1u << 15)> blocks;
	SpscRing<EventRecord, (1u << 15)> events;
	std::mutex textMutex;
	std::vector<std::string> pendingText;
	std::thread worker;
	std::atomic<bool> running {false};
	std::string dir;
};

}
