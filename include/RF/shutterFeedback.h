#ifndef YOKIS_SHUTTER_FEEDBACK_H
#define YOKIS_SHUTTER_FEEDBACK_H

#include "reliability.h"
#include "RF/stopVerification.h"

// Values kept identical to the legacy DeviceStatus enum.
enum DeviceStatus { OFF = 0, ON, UNDEFINED, SHUTTER_OPENING, SHUTTER_CLOSING,
                    SHUTTER_STOPPED, SHUTTER_OPENED, SHUTTER_CLOSED };

namespace Yokis {
// Context belongs to each Device, NOT the shared E2bp radio. This restores the
// user's local STOP heuristic (effective window: 5000ms, not the old comment's
// 500ms). A simple resting reply is an estimate, never a proved limit switch.
class ShutterFeedback {
public:
    static const uint32_t StopWindowMs = 5000;
    enum Action : uint8_t { NoAction, Up, Down, Pause, Toggle };
    enum Source : uint8_t { Unknown, RadioStatus, SimpleEstimate,
                            CommandEstimate, StopEstimate };

    ShutterFeedback() : state_(UNDEFINED), source_(Unknown), last_(NoAction),
        commandAt_(0), commandResponse_(false), pending_(false),
        stopLatched_(false), pauseArmed_(false), uncertain_(false),
        rawValid_(false), rawCommand_(false), raw0_(0), raw1_(0) {}

    void begin(Action action, uint32_t now) {
        last_ = action; commandAt_ = now; commandResponse_ = false;
        pending_ = true; rawValid_ = false; verification_.cancel();
        // A new direction must clear a previous STOP even if polling misses
        // the whole movement. A new toggle has unknown direction.
        if (action != Pause) { stopLatched_ = false; pauseArmed_ = false; }
    }

    DeviceStatus finish(bool response, uint32_t now) {
        pending_ = false; commandResponse_ = response;
        if (last_ == Pause) verification_.start(now);
        if (!response) {
            stopLatched_ = pauseArmed_ = false;
            uncertain_ = true; return set(UNDEFINED, Unknown);
        }
        uncertain_ = false;
        switch (last_) {
            case Up: return set(SHUTTER_OPENING, CommandEstimate);
            case Down: return set(SHUTTER_CLOSING, CommandEstimate);
            case Pause:
                pauseArmed_ = true;
                // A STOP reply can still describe motion BEFORE execution.
                // Only latch after a resting response, or an existing latch.
                if (rawValid_ && raw0_ <= 1 && raw1_ == 0 &&
                    !elapsed(now, commandAt_, StopWindowMs)) stopLatched_ = true;
                return set(SHUTTER_STOPPED, CommandEstimate);
            default: return set(UNDEFINED, Unknown);
        }
    }

    DeviceStatus observe(uint8_t a, uint8_t b, uint32_t now) {
        rawValid_ = true; raw0_ = a; raw1_ = b;
        rawCommand_ = pending_;
        // Direct-command replies can contain the old state. Preserve raw bytes
        // for diagnostics, but do not turn them into a post-command endpoint.
        if (pending_) return UNDEFINED;
        expireVerification(now);

        // Preserve the existing rich-response masks. No speculative masking
        // of 0x40/0x80 or automatic device-generation detection is introduced.
        if ((a & 0x21) == 0x21 && b == 2) return rich(SHUTTER_OPENED);
        if ((a & 0x11) == 0x10 && b == 2) return rich(SHUTTER_CLOSED);
        if (b == 3 || (a <= 1 && b == 1)) {
            // Real motion, including motion initiated by another remote,
            // invalidates a previously latched stop.
            // Early motion after STOP may predate its execution. Keep the
            // current verification armed until a resting observation or budget.
            if (stopLatched_ && !verification_.pending()) pauseArmed_ = false;
            stopLatched_ = false; uncertain_ = false;
            return set((a & 1) ? SHUTTER_OPENING : SHUTTER_CLOSING, RadioStatus);
        }
        if (b == 2) {
            uncertain_ = false; stopLatched_ = true;
            verification_.finish(StopVerification::StoppedObserved);
            return set(SHUTTER_STOPPED, RadioStatus);
        }
        if (a <= 1 && b == 0) {
            if (uncertain_) return set(UNDEFINED, Unknown);
            if (stopLatched_ || (pauseArmed_ && commandResponse_ &&
                (verification_.pending() || !elapsed(now, commandAt_, StopWindowMs)))) {
                stopLatched_ = true;
                verification_.finish(StopVerification::StoppedObserved);
                return set(SHUTTER_STOPPED, StopEstimate);
            }
            pauseArmed_ = false;
            return set(a ? SHUTTER_OPENED : SHUTTER_CLOSED, SimpleEstimate);
        }
        return set(UNDEFINED, Unknown);
    }

    const StopVerification& verification() const { return verification_; }
    bool expireVerification(uint32_t now) {
        if (!verification_.expired(now)) return false;
        verification_.finish(StopVerification::Inconclusive);
        stopLatched_ = pauseArmed_ = false; uncertain_ = true;
        set(UNDEFINED, Unknown);
        return true;
    }
    bool beginVerificationPoll(uint32_t now) {
        expireVerification(now);
        return verification_.beginAttempt();
    }
    DeviceStatus finishVerificationPoll(bool response, uint32_t now) {
        if (!response) { rawValid_ = false; set(UNDEFINED, Unknown); }
        verification_.retry(now);
        if (verification_.result() == StopVerification::Inconclusive) {
            stopLatched_ = pauseArmed_ = false; uncertain_ = true;
            // A received motion is useful evidence; no response is not.
            if (!response || (state_ != SHUTTER_OPENING && state_ != SHUTTER_CLOSING))
                set(UNDEFINED, Unknown);
        }
        return state_;
    }

    // A timeout/unknown response must not leave stale position metadata. It
    // does not erase the last attempted command, useful for troubleshooting.
    void unknown() { state_ = UNDEFINED; source_ = Unknown; }
    DeviceStatus state() const { return state_; }
    Source source() const { return source_; }
    bool estimated() const {
        return source_ == SimpleEstimate || source_ == CommandEstimate ||
               source_ == StopEstimate;
    }
    bool pending() const { return pending_; }
    bool commandResponse() const { return commandResponse_; }
    uint32_t commandAt() const { return commandAt_; }
    bool hasCommand() const { return last_ != NoAction; }
    bool rawValid() const { return rawValid_; }
    uint8_t raw0() const { return raw0_; }
    uint8_t raw1() const { return raw1_; }
    const char* rawOrigin() const { return !rawValid_ ? "none" : rawCommand_ ? "command_reply" : "poll"; }
    const char* commandName() const {
        switch (last_) {
            case Up: return "UP"; case Down: return "DOWN";
            case Pause: return "PAUSE"; case Toggle: return "TOGGLE";
            default: return "NONE";
        }
    }
    const char* sourceName() const {
        switch (source_) {
            case RadioStatus: return "rf_status";
            case SimpleEstimate: return "simple_rf_estimate";
            case CommandEstimate: return "command_estimate";
            case StopEstimate: return "command_stop_estimate";
            default: return "unknown";
        }
    }
private:
    DeviceStatus set(DeviceStatus s, Source source) { state_ = s; source_ = source; return s; }
    DeviceStatus rich(DeviceStatus s) {
        stopLatched_ = pauseArmed_ = uncertain_ = false;
        verification_.finish(StopVerification::EndpointObserved);
        return set(s, RadioStatus);
    }
    StopVerification verification_;
    DeviceStatus state_;
    Source source_;
    Action last_;
    uint32_t commandAt_;
    bool commandResponse_, pending_, stopLatched_, pauseArmed_, uncertain_;
    bool rawValid_, rawCommand_;
    uint8_t raw0_, raw1_;
};
}
#endif
