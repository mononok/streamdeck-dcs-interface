//==============================================================================
/**
@file       MyStreamDeckPlugin.cpp

@brief      Based off of Elgato's CPU plugin example

@copyright  (c) 2018, Corsair Memory, Inc.
                        This source code is licensed under the MIT-style license found in the LICENSE file.

**/
//==============================================================================

#include "MyStreamDeckPlugin.h"
#include <atomic>

#include "Common/EPLJSONUtils.h"
#include "Common/ESDConnectionManager.h"
#include "DcsInterface/DcsIdLookup.h"
#include "DcsInterface/DcsInterfaceParameters.h"
/****************************************************************************/
/*                                                                          */
/*                                                                          */
/*                                                                          */
/****************************************************************************/
/*****************************************************************************
** FUNCTION :
** PARAMETER:                       :                                   (IO)
** RETURN   : エラーコード
** AUTHOR   : Yoshiharu Oba (Justec)
** CREATE   : 2020/  /
** UPDATE   :     /  /
**----------------------------------------------------------------------------
** COMMENT  :
*****************************************************************************/
class CallBackTimer {
  public:
    CallBackTimer() : _execute(false) {}

    ~CallBackTimer() {
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
        _execute.store(true, std::memory_order_release);
        _thd = std::thread([this, interval, func]() {
            while (_execute.load(std::memory_order_acquire)) {
                func();
                std::this_thread::sleep_for(std::chrono::milliseconds(interval));
            }
        });
    }

    bool is_running() const noexcept { return (_execute.load(std::memory_order_acquire) && _thd.joinable()); }

  private:
    std::atomic<bool> _execute;
    std::thread _thd;
};
/*----------------------------------------------------------------------------
**
**
**
*/
/*--------------------------------------------------------------------------*/
/*                                                                          */
/*                                                                          */
/*                                                                          */
/*--------------------------------------------------------------------------*/
/******************************************************************************
 *      Function    :
 *      Description : コンストラクター
 *      Parameter   :                       :
 *      Return val  :
 *      Notes       :
 */
MyStreamDeckPlugin::MyStreamDeckPlugin() {
    mTimer = new CallBackTimer();
    mTimer->start(10, [this]() { this->UpdateFromGameState(); });
}
/******************************************************************************
 *      Function    :
 *      Description : デコンストラクター
 *      Parameter   :                       :
 *      Return val  :
 *      Notes       :
 */
MyStreamDeckPlugin::~MyStreamDeckPlugin() {
    if (mTimer != nullptr) {
        mTimer->stop();

        delete mTimer;
        mTimer = nullptr;
    }
    if (dcs_interface_ != nullptr) {
        delete dcs_interface_;
        dcs_interface_ = nullptr;
    }
}
/******************************************************************************
 *      Function    : get_connection_settings
 *      Description :
 *      Parameter   : global_settings       :
 *      Return val  :
 *      Notes       :
 */
DcsConnectionSettings MyStreamDeckPlugin::get_connection_settings(const json &global_settings) {
    const std::string ip_address_request = EPLJSONUtils::GetStringByName(global_settings, "ip_address");
    const std::string ip_address_partner = EPLJSONUtils::GetStringByName(global_settings, "ip_address_partner");
    const std::string listener_port_request = EPLJSONUtils::GetStringByName(global_settings, "listener_port");
    const std::string send_port_request = EPLJSONUtils::GetStringByName(global_settings, "send_port");
    const bool user_settings_valid =
        (!ip_address_request.empty() && !ip_address_partner.empty() && !listener_port_request.empty() && !send_port_request.empty());

    DcsConnectionSettings connection_settings;
    if (user_settings_valid) {
        connection_settings.rx_port = listener_port_request;
        connection_settings.tx_port = send_port_request;
        connection_settings.ip_address = ip_address_request;
        connection_settings.ip_address_partner = ip_address_partner;
    } else {
        connection_settings.rx_port = kDefaultDcsListenerPort;
        connection_settings.tx_port = kDefaultDcsSendPort;
        connection_settings.ip_address = kDefaultDcsIpAddress;
        connection_settings.ip_address_partner = kDefaultDcsIpAddress;
    }
    return connection_settings;
}
/******************************************************************************
 *      Function    : DidReceiveGlobalSettings (規定)
 *      Description : getGlobalSettings()後のデータ受け取りコールバック関数
 *      Parameter   : inPayload             :
 *      Return val  : なし
 *      Notes       : ※ 関数はユーザー定義する
 */
void MyStreamDeckPlugin::DidReceiveGlobalSettings(const json &inPayload) {
    json settings;
    EPLJSONUtils::GetObjectByName(inPayload, "settings", settings);
    DcsConnectionSettings connection_settings = get_connection_settings(settings);

    // If settings have changed, close DcsInterface so it can be re-opened with new connection.
    if (dcs_interface_ != nullptr && !dcs_interface_->connection_settings_match(connection_settings)) {
        delete dcs_interface_;
        dcs_interface_ = nullptr;
    }

    // Create first instance of DCS Interface only done under DidReceiveGlobalSettings to allow for any stored settings
    // to be used before binding socket to default port values.
    if (dcs_interface_ == nullptr) {
        try {
            dcs_interface_ = new DcsInterface(connection_settings);
        } catch (const std::exception &e) {
            mConnectionManager->LogMessage("Caught Exception While Opening Connection: " + std::string(e.what()));
        }
    }
}
/******************************************************************************
 *      Function    : UpdateFromGameState
 *      Description : タイマーより呼び出しあり
 *      Parameter   : なし
 *      Return val  : なし
 *      Notes       :
 */
void MyStreamDeckPlugin::UpdateFromGameState() {
    //
    // Warning: UpdateFromGameState() is running in the timer thread
    //

    if (dcs_interface_ != nullptr) {
        // Update the DCS game state in memory, then update each Streamdeck button context.
        dcs_interface_->update_dcs_state();

        if (mConnectionManager != nullptr) {
            mVisibleContextsMutex.lock();
            for (auto &elem : mVisibleContexts) {
                elem.second.updateContextState(dcs_interface_, mConnectionManager);
            }
            mVisibleContextsMutex.unlock();
        }
    }
}
/******************************************************************************
 *      Function    : KeyDownForAction (規定)
 *      Description : キーが押された
 *      Parameter   : inAction              :
 *                  : inContext             :
 *                  : inPayload             :
 *                  : inDeviceID            :
 *      Return val  : なし
 *      Notes       :
 */
void MyStreamDeckPlugin::KeyDownForAction(const std::string &inAction,
                                          const std::string &inContext,
                                          const json &inPayload,
                                          const std::string &inDeviceID) {
    if (dcs_interface_ != nullptr) {
        mVisibleContextsMutex.lock();
        mVisibleContexts[inContext].handleButtonEvent(dcs_interface_, KEY_DOWN, inAction, inPayload);
        mVisibleContextsMutex.unlock();
    }
}
/******************************************************************************
 *      Function    : KeyUpForAction (規定)
 *      Description : キーが離された
 *      Parameter   : inAction              :
 *                  : inContext             :
 *                  : inPayload             :
 *                  : inDeviceID            :
 *      Return val  : なし
 *      Notes       :
 */
void MyStreamDeckPlugin::KeyUpForAction(const std::string &inAction,
                                        const std::string &inContext,
                                        const json &inPayload,
                                        const std::string &inDeviceID) {

    if (dcs_interface_ != nullptr) {
        mVisibleContextsMutex.lock();
        // The Streamdeck will by default change a context's state after a KeyUp event, so a force send of the current
        // context's state will keep the button state in sync with the plugin.
        if (inAction.find("switch") != std::string::npos) {
            // For switches use a delay to avoid jittering and a race condition of Streamdeck and Plugin trying to
            // change state.
            mVisibleContexts[inContext].forceSendStateAfterDelay(3);
        } else {
            mVisibleContexts[inContext].forceSendState(mConnectionManager);
        }
        mVisibleContexts[inContext].handleButtonEvent(dcs_interface_, KEY_UP, inAction, inPayload);
        mVisibleContextsMutex.unlock();
    }
}
/******************************************************************************
 *      Function    : WillAppearForAction (規定)
 *      Description : インスタンスがStreamDeckに表示された
 *      Parameter   : inAction              :
 *                  : inContext             :
 *                  : inPayload             :
 *                  : inDeviceID            :
 *      Return val  : なし
 *      Notes       :
 */
void MyStreamDeckPlugin::WillAppearForAction(const std::string &inAction,
                                             const std::string &inContext,
                                             const json &inPayload,
                                             const std::string &inDeviceID) {
    // Remember the context.
    mVisibleContextsMutex.lock();
    json settings;
    EPLJSONUtils::GetObjectByName(inPayload, "settings", settings);
    mVisibleContexts[inContext] = StreamdeckContext(inContext, settings);
    if (dcs_interface_ != nullptr) {
        mVisibleContexts[inContext].forceSendState(mConnectionManager);
    }
    mVisibleContextsMutex.unlock();
}
/******************************************************************************
 *      Function    : WillDisappearForAction (規定)
 *      Description : インスタンスがStreamDeckから削除された
 *      Parameter   : inAction              :
 *                  : inContext             :
 *                  : inPayload             :
 *                  : inDeviceID            :
 *      Return val  : なし
 *      Notes       :
 */
void MyStreamDeckPlugin::WillDisappearForAction(const std::string &inAction,
                                                const std::string &inContext,
                                                const json &inPayload,
                                                const std::string &inDeviceID) {
    // Remove the context.
    mVisibleContextsMutex.lock();
    mVisibleContexts.erase(inContext);
    mVisibleContextsMutex.unlock();
}
/******************************************************************************
 *      Function    : DeviceDidConnect (規定)
 *      Description : PCにデバイスを接続した。
 *      Parameter   : inDeviceID            :
 *                  : inDeviceInfo          :
 *      Return val  : なし
 *      Notes       :
 */
void MyStreamDeckPlugin::DeviceDidConnect(const std::string &inDeviceID, const json &inDeviceInfo) {
    // Request global settings from Streamdeck.
    if (mConnectionManager != nullptr) {
        mConnectionManager->GetGlobalSettings();
    }
}
/******************************************************************************
 *      Function    : DeviceDidDisconnect (規定)
 *      Description : PCからデバイスを削除した
 *      Parameter   : inDeviceID            :
 *      Return val  : なし
 *      Notes       :
 */
void MyStreamDeckPlugin::DeviceDidDisconnect(const std::string &inDeviceID) {
    // Nothing to do.
}
/******************************************************************************
 *      Function    : SendToPlugin (規定)
 *      Description : PropertyInspectorからのsendToPluginイベント処理
 *      Parameter   : inAction              :
 *                  : inContext             :
 *                  : inPayload             :
 *                  : inDeviceID            :
 *      Return val  : なし
 *      Notes       :
 */
void MyStreamDeckPlugin::SendToPlugin(const std::string &inAction,
                                      const std::string &inContext,
                                      const json &inPayload,
                                      const std::string &inDeviceID) {
    const std::string event = EPLJSONUtils::GetStringByName(inPayload, "event");

    if (event == "SettingsUpdate") {
        // Update settings for the specified context -- triggered by Property Inspector detecting a change.
        mVisibleContextsMutex.lock();
        if (mVisibleContexts.count(inContext) > 0) {
            mVisibleContexts[inContext].updateContextSettings(inPayload["settings"]);
        }
        mVisibleContextsMutex.unlock();
    }

    if (event == "RequestDcsStateUpdate") {
        if (dcs_interface_ == nullptr) {
            mConnectionManager->SendToPropertyInspector(inAction,
                                                        inContext,
                                                        json({{"event", "DebugDcsGameState"},
                                                              {"current_game_state", ""},
                                                              {"error", "DcsInterface not connected"}}));

        } else {
            const std::map<int, std::string> dcs_id_values = dcs_interface_->debug_get_current_game_state();
            json current_game_state;
            for (const auto &[key, value] : dcs_id_values) {
                current_game_state[std::to_string(key)] = value;
            }
            mConnectionManager->SendToPropertyInspector(
                inAction,
                inContext,
                json({{"event", "DebugDcsGameState"}, {"current_game_state", current_game_state}}));
        }
    }

    if (event == "RequestInstalledModules") {
        const std::string dcs_install_path = EPLJSONUtils::GetStringByName(inPayload, "dcs_install_path");
        const std::string modules_subdir = "/mods/aircraft/";
        const json installed_modules_and_result = get_installed_modules(dcs_install_path, modules_subdir);
        const std::string result = EPLJSONUtils::GetStringByName(installed_modules_and_result, "result");
        if (result != "success") {
            mConnectionManager->LogMessage("Get Installed Modules Failure: " + result);
        }
        mConnectionManager->SendToPropertyInspector(
            inAction,
            inContext,
            json({{"event", "InstalledModules"},
                  {"installed_modules", installed_modules_and_result["installed_modules"]}}));
    }

    if (event == "RequestIdLookup") {
        const std::string dcs_install_path = EPLJSONUtils::GetStringByName(inPayload, "dcs_install_path");
        const std::string module = EPLJSONUtils::GetStringByName(inPayload, "module");
        json clickabledata_and_result = get_clickabledata(dcs_install_path, module, "extract_clickabledata.lua");
        const std::string lua_result = EPLJSONUtils::GetStringByName(clickabledata_and_result, "result");
        if (lua_result != "success") {
            mConnectionManager->LogMessage(module + " Clickabledata Result: " + lua_result);
        }
        mConnectionManager->SendToPropertyInspector(
            inAction,
            inContext,
            json({{"event", "Clickabledata"}, {"clickabledata", clickabledata_and_result["clickabledata_items"]}}));
    }
}
