#include <chrono>
#include <optional>

class IntervalEMA {
public:
    using clock    = std::chrono::steady_clock;
    using duration = std::chrono::duration<double>;

    // alpha in (0,1]. Roughly, effective window ~ 2/alpha - 1 (rule of thumb).
    explicit IntervalEMA(double alpha) : alpha_(alpha) {}

    void tick() {
        auto now = clock::now();
        if (last_) {
            duration d = now - *last_;
            if (!ema_) ema_ = d;
            else       ema_ = alpha_ * d + (1.0 - alpha_) * *ema_;
        }
        last_ = now;
    }

    std::optional<duration> value() const { return ema_; }

private:
    double alpha_;
    std::optional<clock::time_point> last_;
    std::optional<duration> ema_;
};
