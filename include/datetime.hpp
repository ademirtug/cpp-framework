// datetime.h
#ifndef DATETIME_H
#define DATETIME_H

#include <chrono>
#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <stdexcept>

namespace framework {

    class datetime {
    private:
        // Use standard time_point type
        using seconds_tp = std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>;
        seconds_tp tp_;

        // Helper: parse ISO 8601 string like "2023-10-05T12:34:56Z"
        static seconds_tp parse(const std::string& str) {
            std::istringstream ss(str);
            std::chrono::sys_time<std::chrono::seconds> t;
            ss >> std::chrono::parse("%Y-%m-%dT%H:%M:%SZ", t);
            if (ss.fail()) {
                throw std::invalid_argument("Invalid datetime format: '" + str + "'");
            }
            return t;
        }


        // ✅ Portable replacement for to_tm: convert time_point to std::tm in UTC
        std::tm to_tm_utc() const {
            std::time_t t = std::chrono::system_clock::to_time_t(tp_);
            std::tm tm_snapshot;
#ifdef _WIN32
            gmtime_s(&tm_snapshot, &t); // Secure version on Windows
#else
            gmtime_r(&t, &tm_snapshot); // POSIX
#endif
            return tm_snapshot;
        }

    public:
        // Default constructor: current time
        datetime()
            : tp_(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())) {
        }

        // Construct from time_t
        explicit datetime(std::time_t t)
            : tp_(std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::from_time_t(t))) {
        }

        // Construct from ISO string
        explicit datetime(const std::string& str)
            : tp_(parse(str)) {
        }

        // Copy/move
        datetime(const datetime&) = default;
        datetime& operator=(const datetime&) = default;
        datetime(datetime&&) = default;
        datetime& operator=(datetime&&) = default;

        ~datetime() = default;

        // Static now()
        static datetime now() {
            return datetime{};
        }

        // Format to string (default: ISO 8601)
        std::string tostring(const std::string& fmt = "%Y-%m-%dT%H:%M:%SZ") const {
            return std::vformat(fmt, std::make_format_args(tp_));
        }

        // Assignment from string
        datetime& operator=(const std::string& str) {
            tp_ = parse(str);
            return *this;
        }

        // Arithmetic: use chrono durations
        datetime& add_seconds(std::int64_t seconds) {
            tp_ += std::chrono::seconds(seconds);
            return *this;
        }

        datetime& add_minutes(std::int64_t minutes) {
            tp_ += std::chrono::minutes(minutes);
            return *this;
        }

        datetime& add_hours(std::int64_t hours) {
            tp_ += std::chrono::hours(hours);
            return *this;
        }

        datetime& add_days(std::int64_t days) {
            tp_ += std::chrono::days(days);
            return *this;
        }

        datetime& add_months(int months) {
            auto days = std::chrono::floor<std::chrono::days>(tp_);
            auto ymd = std::chrono::year_month_day{ days };
            ymd = ymd + std::chrono::months{ months }; // exact, handles varying month lengths
            tp_ = std::chrono::sys_days{ ymd } + (tp_ - days); // preserve time-of-day
            return *this;
        }

        datetime& add_years(int years) {
            auto days = std::chrono::floor<std::chrono::days>(tp_);
            auto ymd = std::chrono::year_month_day{ days };
            ymd = ymd + std::chrono::years{ years };
            tp_ = std::chrono::sys_days{ ymd } + (tp_ - days);
            return *this;
        }

        // Getters: extract from time_point using calendar types (C++20)
        int hour() const {
            auto sys_days = std::chrono::floor<std::chrono::days>(tp_);
            auto time_in_day = tp_ - sys_days;
            auto hms = std::chrono::hh_mm_ss{ time_in_day };
            return static_cast<int>(hms.hours().count());
        }

        int minute() const {
            auto sys_days = std::chrono::floor<std::chrono::days>(tp_);
            auto time_in_day = tp_ - sys_days;
            auto hms = std::chrono::hh_mm_ss{ time_in_day };
            return static_cast<int>(hms.minutes().count());
        }

        int second() const {
            auto sys_days = std::chrono::floor<std::chrono::days>(tp_);
            auto time_in_day = tp_ - sys_days;
            auto hms = std::chrono::hh_mm_ss{ time_in_day };
            return static_cast<int>(hms.seconds().count());
        }

        int year() const {
            auto ymd = std::chrono::year_month_day{ std::chrono::floor<std::chrono::days>(tp_) };
            return static_cast<int>(ymd.year());
        }

        int month() const {
            auto ymd = std::chrono::year_month_day{ std::chrono::floor<std::chrono::days>(tp_) };
            return static_cast<unsigned>(ymd.month());
        }

        int day() const {
            auto ymd = std::chrono::year_month_day{ std::chrono::floor<std::chrono::days>(tp_) };
            return static_cast<unsigned>(ymd.day());
        }

        // Comparison operators
        bool operator==(const datetime& other) const { return tp_ == other.tp_; }
        bool operator!=(const datetime& other) const { return tp_ != other.tp_; }
        bool operator<(const datetime& other) const { return tp_ < other.tp_; }
        bool operator<=(const datetime& other) const { return tp_ <= other.tp_; }
        bool operator>(const datetime& other) const { return tp_ > other.tp_; }
        bool operator>=(const datetime& other) const { return tp_ >= other.tp_; }

        // Conversion to time_t
        std::time_t to_time_t() const {
            return std::chrono::system_clock::to_time_t(tp_);
        }

        // Access underlying time_point
        seconds_tp time_point() const {
            return tp_;
        }
    };

    // User-defined literal
    inline datetime operator""_dt(const char* str, size_t) {
        return datetime(std::string(str));
    }
}
#endif // DATETIME_H