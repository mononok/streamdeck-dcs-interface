// Copyright 2020 Charles Tytler

#include "pch.h"

#include "StreamdeckContext.h"

#include "../Common/EPLJSONUtils.h"

class HoldingDownTimer {
  public:
    HoldingDownTimer() : _execute(false) {}

    ~HoldingDownTimer() {
        if (_execute.load(std::memory_order_acquire)) {
            stop();
        };
    }

    void stop() {
        _execute.store(false, std::memory_order_release);
        if (_thd.joinable())
            _thd.join();
    }

    void start(int interval, std::function<void(void)> func) {
        if (_execute.load(std::memory_order_acquire)) {
            stop();
        };
        _timeout = false;
        _execute.store(true, std::memory_order_release);
        _thd = std::thread([this, interval, func]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));
            if (_execute.load(std::memory_order_acquire)) {
                func();
                _timeout = true;
            }
        });
    }

    bool is_running() const noexcept { return (_execute.load(std::memory_order_acquire) && _thd.joinable()); }

    bool    is_timeout() { return( _timeout ); }

  private:
    std::atomic<bool> _execute;
    std::thread _thd;
    int         _timeout = false;
};


StreamdeckContext::StreamdeckContext(const std::string &context) { context_ = context; }

StreamdeckContext::StreamdeckContext(const std::string &context, const json &settings) {
    context_ = context;
    updateContextSettings(settings);
}

void StreamdeckContext::updateContextState(DcsInterface *dcs_interface, ESDConnectionManager *mConnectionManager) {
    // Initialize to default values.
    ContextState updated_state = FIRST;
    std::string updated_title = "";

    if (increment_monitor_is_set_) {
        const std::string current_game_value_raw = dcs_interface->get_value_of_dcs_id(dcs_id_increment_monitor_);
        if (is_number(current_game_value_raw)) {
            current_increment_value_ = Decimal(current_game_value_raw);
        }
    }

    if (compare_monitor_is_set_) {
        const std::string current_game_value_raw = dcs_interface->get_value_of_dcs_id(dcs_id_compare_monitor_);
        if (is_number(current_game_value_raw)) {
            updated_state = determineStateForCompareMonitor(Decimal(current_game_value_raw));
        }
    }
    if (string_monitor_is_set_) {
        const std::string current_game_string_value = dcs_interface->get_value_of_dcs_id(dcs_id_string_monitor_);
        if (!current_game_string_value.empty()) {
            updated_title = determineTitleForStringMonitor(current_game_string_value);
        }
    }

    if (updated_state != current_state_) {
        current_state_ = updated_state;
        mConnectionManager->SetState(static_cast<int>(current_state_), context_);
    }
    if (updated_title != current_title_) {
        current_title_ = updated_title;
        mConnectionManager->SetTitle(current_title_, context_, kESDSDKTarget_HardwareAndSoftware);
    }

    if (delay_for_force_send_state_) {
        if (delay_for_force_send_state_.value()-- <= 0) {
            mConnectionManager->SetState(static_cast<int>(current_state_), context_);
            delay_for_force_send_state_.reset();
        }
    }
}

void StreamdeckContext::forceSendState(ESDConnectionManager *mConnectionManager) {
    mConnectionManager->SetState(static_cast<int>(current_state_), context_);
}

void StreamdeckContext::forceSendStateAfterDelay(const int delay_count) {
    delay_for_force_send_state_.emplace(delay_count);
}

void StreamdeckContext::updateContextSettings(const json &settings) {
    // Read in settings.
    const std::string dcs_id_increment_monitor_raw =
        EPLJSONUtils::GetStringByName(settings, "dcs_id_increment_monitor");
    const std::string dcs_id_compare_monitor_raw = EPLJSONUtils::GetStringByName(settings, "dcs_id_compare_monitor");
    const std::string dcs_id_compare_condition_raw =
        EPLJSONUtils::GetStringByName(settings, "dcs_id_compare_condition");
    const std::string dcs_id_comparison_value_raw = EPLJSONUtils::GetStringByName(settings, "dcs_id_comparison_value");
    const std::string dcs_id_string_monitor_raw = EPLJSONUtils::GetStringByName(settings, "dcs_id_string_monitor");
    // Set boolean from checkbox using default false value if it doesn't exist in "settings".
    const std::string string_monitor_vertical_spacing_raw =
        EPLJSONUtils::GetStringByName(settings, "string_monitor_vertical_spacing");
    string_monitor_passthrough_ = EPLJSONUtils::GetBoolByName(settings, "string_monitor_passthrough_check", true);
    std::stringstream string_monitor_mapping_raw;
    string_monitor_mapping_raw << EPLJSONUtils::GetStringByName(settings, "string_monitor_mapping");

    // Process status of settings.
    increment_monitor_is_set_ = is_integer(dcs_id_increment_monitor_raw);
    const bool compare_monitor_is_populated = is_integer(dcs_id_compare_monitor_raw);
    const bool comparison_value_is_populated = is_number(dcs_id_comparison_value_raw);
    compare_monitor_is_set_ = compare_monitor_is_populated && comparison_value_is_populated;
    string_monitor_is_set_ = is_integer(dcs_id_string_monitor_raw);

    // Update internal settings of class instance.
    if (increment_monitor_is_set_) {
        dcs_id_increment_monitor_ = std::stoi(dcs_id_increment_monitor_raw);
    }

    if (compare_monitor_is_set_) {
        dcs_id_compare_monitor_ = std::stoi(dcs_id_compare_monitor_raw);
        dcs_id_comparison_value_ = Decimal(dcs_id_comparison_value_raw);
        if (dcs_id_compare_condition_raw == "EQUAL_TO") {
            dcs_id_compare_condition_ = EQUAL_TO;
        } else if (dcs_id_compare_condition_raw == "LESS_THAN") {
            dcs_id_compare_condition_ = LESS_THAN;
        } else // Default in Property Inspector html is GREATER_THAN.
        {
            dcs_id_compare_condition_ = GREATER_THAN;
        }
    }

    if (string_monitor_is_set_) {
        dcs_id_string_monitor_ = std::stoi(dcs_id_string_monitor_raw);
        if (is_integer(string_monitor_vertical_spacing_raw)) {
            string_monitor_vertical_spacing_ = std::stoi(string_monitor_vertical_spacing_raw);
        }
        if (!string_monitor_passthrough_) {
            string_monitor_mapping_.clear();
            std::pair<std::string, std::string> key_and_value;
            while (pop_key_and_value(string_monitor_mapping_raw, ',', '=', key_and_value)) {
                string_monitor_mapping_[key_and_value.first] = key_and_value.second;
            }
        }
    }
}

void StreamdeckContext::handleButtonEvent(DcsInterface *dcs_interface,
                                          const KeyEvent event,
                                          const std::string &action,
                                          const json &inPayload) {
    const std::string button_id = EPLJSONUtils::GetStringByName(inPayload["settings"], "button_id");
    const std::string device_id = EPLJSONUtils::GetStringByName(inPayload["settings"], "device_id");

    // Set boolean from checkbox using default false value if it doesn't exist in "settings".
    cycle_increments_is_allowed_ =
        EPLJSONUtils::GetBoolByName(inPayload["settings"], "increment_cycle_allowed_check", false);

    if (is_integer(button_id) && is_integer(device_id)) {
        bool send_command = false;
        std::string value = "";
        if (action.find("switch") != std::string::npos) {
            const ContextState state = EPLJSONUtils::GetIntByName(inPayload, "state") == 0 ? FIRST : SECOND;
            send_command = determineSendValueForSwitch(event, state, inPayload["settings"], value);
        } else if (action.find("3states") != std::string::npos) {
            const ContextState state = EPLJSONUtils::GetIntByName(inPayload, "state") == 0 ? FIRST : SECOND;
            send_command = determineSendValueFor3States(event, state, inPayload["settings"], value, button_id, device_id, dcs_interface );
        } else if (action.find("increment") != std::string::npos) {
            send_command = determineSendValueForIncrement(event, inPayload["settings"], value);
        } else {
            send_command = determineSendValueForMomentary(event, inPayload["settings"], value);
        }

        if (send_command) {
            dcs_interface->send_dcs_command(std::stoi(button_id), device_id, value);
        }
    }
}

StreamdeckContext::ContextState StreamdeckContext::determineStateForCompareMonitor(const Decimal &current_game_value) {
    bool set_context_state_to_second = false;
    switch (dcs_id_compare_condition_) {
    case EQUAL_TO:
        set_context_state_to_second = (current_game_value == dcs_id_comparison_value_);
        break;
    case LESS_THAN:
        set_context_state_to_second = (current_game_value < dcs_id_comparison_value_);
        break;
    case GREATER_THAN:
        set_context_state_to_second = (current_game_value > dcs_id_comparison_value_);
        break;
    }

    return set_context_state_to_second ? SECOND : FIRST;
}

std::string StreamdeckContext::determineTitleForStringMonitor(const std::string &current_game_string_value) {
    std::string title;
    if (string_monitor_passthrough_) {
        title = current_game_string_value;
    } else {
        title = string_monitor_mapping_[current_game_string_value];
    }
    // Apply vertical spacing.
    if (string_monitor_vertical_spacing_ < 0) {
        for (int i = 0; i > string_monitor_vertical_spacing_; --i) {
            title = "\n" + title;
        }
    } else {
        for (int i = 0; i < string_monitor_vertical_spacing_; ++i) {
            title = title + "\n";
        }
    }
    return title;
}

bool StreamdeckContext::determineSendValueForMomentary(const KeyEvent event, const json &settings, std::string &value) {
    if (event == KEY_DOWN) {
        value = EPLJSONUtils::GetStringByName(settings, "press_value");
    } else {
        if (!EPLJSONUtils::GetBoolByName(settings, "disable_release_check")) {
            value = EPLJSONUtils::GetStringByName(settings, "release_value");
        }
    }
    const bool is_valid = value.empty() ? false : true;
    return is_valid;
}

bool StreamdeckContext::determineSendValueForSwitch(const KeyEvent event,
                                                    const ContextState state,
                                                    const json &settings,
                                                    std::string &value) {
    if (event == KEY_UP) {
        if (state == FIRST) {
            value = EPLJSONUtils::GetStringByName(settings, "send_when_first_state_value");
        } else {
            value = EPLJSONUtils::GetStringByName(settings, "send_when_second_state_value");
        }

        if (!value.empty()) {
            return true;
        }
    }
    // Switch type only needs to send command on key down.
    return false;
}

bool StreamdeckContext::determineSendValueFor3States(const KeyEvent event,
                                                    const ContextState state,
                                                    const json &settings,
                                                    std::string &value,
                                                    const std::string &button_id,
                                                    const std::string &device_id,
                                                    DcsInterface *dcs_interface ) {
    if (event == KEY_DOWN ) {
        if( HoldingDownTimer_ == nullptr ) {
            HoldingDownButton_id = button_id;
            HoldingDownDevice_id = device_id;
            HoldingDownDcsInterface_ = dcs_interface;
            HoldingDownValue_ = EPLJSONUtils::GetStringByName(settings, "send_when_holding_down_state_value");
            HoldingDownTimer_ = new HoldingDownTimer();
            HoldingDownTimer_->start( 1500, [this]() { this->HoldingDownOccure();});
        }
    }
    else if (event == KEY_UP) {
        if( HoldingDownTimer_ != nullptr ) {
            HoldingDownTimer_->stop();
            if( !HoldingDownTimer_->is_timeout() ) {
                if (state == FIRST) {
                    value = EPLJSONUtils::GetStringByName(settings, "send_when_first_state_value");
                } else {
                    value = EPLJSONUtils::GetStringByName(settings, "send_when_second_state_value");
                }
            }
            delete HoldingDownTimer_;
            HoldingDownTimer_ = nullptr;
        }

        if (!value.empty()) {
            return true;
        }
    }
    // Switch type only needs to send command on key down.
    return false;
}

bool StreamdeckContext::determineSendValueForIncrement(const KeyEvent event, const json &settings, std::string &value) {
    if (event == KEY_DOWN) {
        const std::string increment_value_str = EPLJSONUtils::GetStringByName(settings, "increment_value");
        const std::string increment_min_str = EPLJSONUtils::GetStringByName(settings, "increment_min");
        const std::string increment_max_str = EPLJSONUtils::GetStringByName(settings, "increment_max");
        const bool cycling_is_allowed = EPLJSONUtils::GetBoolByName(settings, "increment_cycle_allowed_check");
        if (is_number(increment_value_str) && is_number(increment_min_str) && is_number(increment_max_str)) {
            Decimal increment_min(increment_min_str);
            Decimal increment_max(increment_max_str);

            current_increment_value_ += Decimal(increment_value_str);

            if (current_increment_value_ < increment_min) {
                current_increment_value_ = cycle_increments_is_allowed_ ? increment_max : increment_min;
            } else if (current_increment_value_ > increment_max) {
                current_increment_value_ = cycle_increments_is_allowed_ ? increment_min : increment_max;
            }
            value = current_increment_value_.str();
            return true;
        }
    }
    // Increment type only needs to send command on key down.
    return false;
}

void StreamdeckContext::HoldingDownOccure( void ) {

    if (!HoldingDownValue_.empty()) {
        HoldingDownDcsInterface_->send_dcs_command(std::stoi(HoldingDownButton_id), HoldingDownDevice_id, HoldingDownValue_);
    }
}




