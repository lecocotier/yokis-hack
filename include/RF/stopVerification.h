#ifndef YOKIS_STOP_VERIFICATION_H
#define YOKIS_STOP_VERIFICATION_H

#include "reliability.h"

namespace Yokis {
// Scheduling policy, not protocol timing: tune only from measured field traces.
// One request per Device; no heap allocation, no timer callback and no RF here.
class StopVerification {
public:
    static const uint32_t InitialDelayMs = 100;
    static const uint32_t RetryDelayMs = 250;
    static const uint32_t BudgetMs = 5000;
    static const uint8_t MaxAttempts = 3;
    enum Result : uint8_t { None, Waiting, StoppedObserved,
                            EndpointObserved, Inconclusive };

    StopVerification() : result_(None), attempts_(0), started_(0), waitFrom_(0) {}
    void cancel() { result_ = None; attempts_ = 0; started_ = waitFrom_ = 0; }
    void start(uint32_t now) {
        result_ = Waiting; attempts_ = 0; started_ = waitFrom_ = now;
    }
    bool pending() const { return result_ == Waiting; }
    bool expired(uint32_t now) const { return pending() && elapsed(now, started_, BudgetMs); }
    bool due(uint32_t now) const {
        return pending() && !expired(now) &&
            elapsed(now, waitFrom_, attempts_ ? RetryDelayMs : InitialDelayMs);
    }
    // Explicit user status queries may also satisfy a scheduled verification.
    bool beginAttempt() {
        if (!pending()) return false;
        if (attempts_ < MaxAttempts) ++attempts_;
        return true;
    }
    void retry(uint32_t now) {
        if (!pending()) return;
        if (attempts_ >= MaxAttempts || expired(now)) result_ = Inconclusive;
        else waitFrom_ = now;
    }
    void finish(Result result) { if (pending()) result_ = result; }
    Result result() const { return result_; }
    uint8_t attempts() const { return attempts_; }
    uint32_t age(uint32_t now) const { return result_ == None ? 0 : uint32_t(now - started_); }
    const char* name() const {
        switch (result_) {
            case Waiting: return "pending";
            case StoppedObserved: return "stopped_observed";
            case EndpointObserved: return "endpoint_observed";
            case Inconclusive: return "inconclusive";
            default: return "none";
        }
    }
private:
    Result result_;
    uint8_t attempts_;
    uint32_t started_, waitFrom_;
};
}
#endif
