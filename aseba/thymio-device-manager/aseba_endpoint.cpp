#include "aseba_endpoint.h"
#include <aseba/common/utils/utils.h>
#include "aseba_property.h"
#include "fw_update_service.h"
#ifdef MOBSYA_TDM_ENABLE_SERIAL
#    include "serialacceptor.h"
#endif
#ifdef MOBSYA_TDM_ENABLE_USB
#    include "usbacceptor.h"
#endif
#include "aseba_tcpacceptor.h"

#include "aseba_node_registery.h"

namespace mobsya {


aseba_endpoint::~aseba_endpoint() {
    mLogInfo("Destroying endpoint");
    std::for_each(std::begin(m_nodes), std::end(m_nodes), [](auto&& node) { node.second.node->disconnect(); });
    boost::asio::post([ctx = &m_io_context]() {
        auto& registery = boost::asio::use_service<aseba_node_registery>(*ctx);
        registery.unregister_expired_endpoints();
    });
    free_endpoint();
}

aseba_endpoint::aseba_endpoint(boost::asio::io_context& io_context, endpoint_t&& e, endpoint_type type)
    : m_endpoint(std::move(e))
    , m_strand(io_context.get_executor())
    , m_io_context(io_context)
    , m_endpoint_type(type)
    , m_uuid(boost::asio::use_service<uuid_generator>(io_context).generate()) {}

#ifdef MOBSYA_TDM_ENABLE_SERIAL
aseba_endpoint::pointer aseba_endpoint::create_for_serial(boost::asio::io_context& io) {
    auto ptr = std::shared_ptr<aseba_endpoint>(new aseba_endpoint(io, usb_serial_port(io)));
    ptr->m_group = group::make_group_for_endpoint(io, ptr);
    return ptr;
}
#endif
aseba_endpoint::pointer aseba_endpoint::create_for_tcp(boost::asio::io_context& io) {
    auto ptr = std::shared_ptr<aseba_endpoint>(new aseba_endpoint(io, tcp_socket(io)));
    ptr->m_group = group::make_group_for_endpoint(io, ptr);
    return ptr;
}


void aseba_endpoint::start() {
    // Briefly put the dongle (if  any) in configuration
    // Mode to read its settings
    if(is_wireless()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto settings = this->wireless_get_settings();
        mLogInfo("Thymio 2 Dongle Wireless settings: network: {:x}, channel: {}", settings.network_id,
                 settings.channel);
    }

    auto& registery = boost::asio::use_service<aseba_node_registery>(m_io_context);
    registery.register_endpoint(shared_from_this());


    // A newly connected thymio may not be ready yet
    // Delay asking for its node id to let it start up the vm
    // otherwhise it may never get our request.
    schedule_send_ping(boost::posix_time::milliseconds(200));

    read_aseba_message();

    if(needs_health_check())
        schedule_nodes_health_check();
}

void aseba_endpoint::cancel_all_ops() {
    boost::system::error_code ec;
    variant_ns::visit([&ec](auto& underlying) { return underlying.cancel(); }, m_endpoint);
    m_msg_queue.clear();
}

void aseba_endpoint::free_endpoint() {
    variant_ns::visit(
        overloaded{[this](tcp_socket& socket) {
                       boost::asio::use_service<aseba_tcp_acceptor>(m_io_context).free_endpoint(this);
                   }
#ifdef MOBSYA_TDM_ENABLE_SERIAL
                   ,
                   [this](mobsya::usb_serial_port& d) {
                       boost::asio::use_service<serial_acceptor_service>(m_io_context).free_device(d.device_path());
                   }
#endif
#ifdef MOBSYA_TDM_ENABLE_USB
                   ,
                   [this](mobsya::usb_device& d) {
                       if(d.native_handle()) {
                           boost::asio::use_service<usb_acceptor_service>(m_io_context)
                               .free_device(libusb_get_device(d.native_handle()));
                       }
                   }
#endif
        },
        m_endpoint);
}

void aseba_endpoint::stop() {
    // serial().cancel();
    // serial().close();
    // ;
}


void aseba_endpoint::reboot() {
    if(m_nodes.size() == 1) {
        const auto id = m_nodes.begin()->second.node->native_id();
        std::for_each(std::begin(m_nodes), std::end(m_nodes), [](auto&& node) { node.second.node->disconnect(); });
        m_nodes.clear();
        write_message(std::make_shared<Aseba::Reboot>(id));
    }
    m_rebooting = true;
    auto timer = std::make_shared<boost::asio::deadline_timer>(m_io_context);
    timer->expires_from_now(boost::posix_time::seconds(1));
    timer->async_wait([timer, ptr = weak_from_this()](boost::system::error_code ec) {
        if(auto that = ptr.lock()) {
            that->m_rebooting = false;
            that->start();
        }
    });
}

std::optional<std::pair<Aseba::EventDescription, std::size_t>>
aseba_endpoint::get_event(const std::string& name) const {
    auto wname = Aseba::UTF8ToWString(name);
    for(std::size_t i = 0; i < m_defs.events.size(); i++) {
        const auto& event = m_defs.events[i];
        if(event.name == wname) {
            return std::optional<std::pair<Aseba::EventDescription, std::size_t>>(std::pair{event, i});
        }
    }
    return {};
};

std::optional<std::pair<Aseba::EventDescription, std::size_t>> aseba_endpoint::get_event(uint16_t id) const {
    if(m_defs.events.size() <= id)
        return {};
    return std::optional<std::pair<Aseba::EventDescription, std::size_t>>(std::pair{m_defs.events[id], id});
}

boost::system::error_code aseba_endpoint::set_events_table(const group::events_table& events) {
    m_defs.events.clear();
    for(const auto& event : events) {
        if(event.type != event_type::aseba)
            continue;
        m_defs.events.emplace_back(Aseba::UTF8ToWString(event.name), event.size);
    }
    return {};
}

boost::system::error_code aseba_endpoint::set_shared_variables(const variables_map& map) {
    for(auto&& var : map) {
        auto n = Aseba::UTF8ToWString(var.first);
        const auto& p = var.second;
        auto it = std::find_if(m_defs.constants.begin(), m_defs.constants.end(),
                               [n](const Aseba::NamedValue& v) { return n == v.name; });
        if(it != m_defs.constants.end()) {
            m_defs.constants.erase(it);
        }
        if(p.is_null())
            continue;
        if(p.is_integral()) {
            auto v = numeric_cast<int16_t>(property::integral_t(p));
            if(v) {
                m_defs.constants.emplace_back(n, *v);
                continue;
            }
        }
        return make_error_code(error_code::incompatible_variable_type);
    }
    return {};
}

void aseba_endpoint::emit_events(const group::properties_map& map, write_callback&& cb = {}) {
    std::vector<std::shared_ptr<Aseba::Message>> messages;
    messages.reserve(map.size());
    for(auto&& event : map) {
        auto event_def = get_event(event.first);
        if(!event_def) {
            if(cb)
                cb(make_error_code(error_code::no_such_variable));
            return;
        }
        auto bytes = to_aseba_variable(event.second, event_def->first.value);
        if(!bytes) {
            if(cb)
                cb(bytes.error());
            return;
        }
        auto msg = std::make_shared<Aseba::UserMessage>(event_def->second, bytes.value());
        messages.push_back(std::move(msg));
    }
    write_messages(std::move(messages), std::move(cb));
}

void aseba_endpoint::on_event(const Aseba::UserMessage& event, const Aseba::EventDescription& def,
                              const std::chrono::system_clock::time_point& timestamp) {
    group::properties_map events;
    auto it = m_nodes.find(event.source);
    if(it == std::end(m_nodes))
        return;

    // create event list
    auto p = detail::aseba_variable_from_range(event.data);
    if((p.is_integral() && def.value != 1) || p.size() != std::size_t(def.value))
        return;
    events.insert(std::pair{Aseba::WStringToUTF8(def.name), p});

    // broadcast to other endpoints
    group()->on_event_received(shared_from_this(), it->second.node->uuid(), events, timestamp);

    // let the node handle the event (send to connected apps)
    it->second.node->on_event_received(events, timestamp);
}

bool aseba_endpoint::wireless_enable_configuration_mode(bool enable) {
    if(!is_wireless())
        return false;
    if(!m_wireless_dongle_settings) {
        if(!enable)
            return true;
        m_wireless_dongle_settings = std::make_unique<WirelessDongleSettings>();
    }
    if(m_wireless_dongle_settings->cfg_mode != enable) {
        m_wireless_dongle_settings->cfg_mode = enable;
#ifdef MOBSYA_TDM_ENABLE_USB
        if(variant_ns::holds_alternative<usb_device>(m_endpoint)) {
            usb().cancel();
            usb().set_rts(enable);
            usb().set_data_terminal_ready(!enable);
        }
#endif
#ifdef MOBSYA_TDM_ENABLE_SERIAL
        if(variant_ns::holds_alternative<usb_serial_port>(m_endpoint)) {
            boost::asio::use_service<serial_acceptor_service>(m_io_context).pause(true);
            boost::system::error_code ec;
            serial().purge();
            serial().set_rts(enable);
            serial().set_data_terminal_ready(!enable);
            boost::asio::use_service<serial_acceptor_service>(m_io_context).pause(false);
        }
#endif
    }
    if(!enable) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        read_aseba_message();
    }
    return true;
}

bool aseba_endpoint::sync_wireless_dongle_settings(bool flash) {
    if(!wireless_enable_configuration_mode(true))
        return false;
    const auto s = sizeof(&m_wireless_dongle_settings->data);
    auto res = variant_ns::visit(
        [this, s, flash](auto& device) {
            boost::system::error_code ec;
            m_wireless_dongle_settings->data.ctrl = flash ? 0x01 : 0;
            if(device.write_some(boost::asio::buffer(&m_wireless_dongle_settings->data, s), ec) != s)
                return false;
            auto read = device.read_some(boost::asio::buffer(&m_wireless_dongle_settings->data, s), ec);
            if(read < s - 1)
                return false;

            // Notify the registery that this endpoint may have changed
            boost::asio::use_service<aseba_node_registery>(m_io_context).register_endpoint(shared_from_this());
            return true;
        },
        m_endpoint);
    wireless_enable_configuration_mode(false);
    return res;
}

bool aseba_endpoint::wireless_set_settings(uint16_t networkId, uint16_t dongleId, uint8_t channel) {
    m_wireless_dongle_settings->data.ctrl = 0;
    m_wireless_dongle_settings->data.channel = 15 + 5 * channel;
    m_wireless_dongle_settings->data.nodeId = dongleId;
    m_wireless_dongle_settings->data.panId = networkId;
    return sync_wireless_dongle_settings(true);
}

aseba_endpoint::wireless_settings aseba_endpoint::wireless_get_settings() {
    // return {};
    if(!m_wireless_dongle_settings && !sync_wireless_dongle_settings(false))
        return {};
    return {m_wireless_dongle_settings->data.panId, m_wireless_dongle_settings->data.nodeId,
            uint8_t((m_wireless_dongle_settings->data.channel - uint8_t(15)) / uint8_t(5))};
}

bool aseba_endpoint::wireless_cfg_mode_enabled() const {
    return m_wireless_dongle_settings && m_wireless_dongle_settings->cfg_mode;
}

bool aseba_endpoint::upgrade_firmware(
    uint16_t id, std::function<void(boost::system::error_code ec, double progress, bool complete)> cb) {

    if(is_wireless())
        return false;

    m_upgrading_firmware = true;

    auto firmware =
        boost::asio::use_service<firmware_update_service>(m_io_context).firmware_data(aseba_node::node_type::Thymio2);
    firmware.then([id, ptr = shared_from_this(), cb](auto f) {
        // if the firmware if empty let it fail as a special case of invalid data
        auto firmware = f.get();

        variant_ns::visit(
            overloaded{
#ifdef MOBSYA_TDM_ENABLE_USB
                [&ptr, id, &firmware, &cb](usb_device& usb) {
                    boost::asio::use_service<usb_acceptor_service>(ptr->m_io_context).pause(false);
                    mobsya::upgrade_thymio2_endpoint(
                        ptr->usb().native_handle(), firmware, id,
                        [ptr, cb](boost::system::error_code err, double progress, bool complete) {
                            // Make sure we are running in our executor
                            boost::asio::post(ptr->m_strand, [cb, ptr, err, progress, complete]() {
                                cb(err, progress, complete);
                                // mark the associated device path as unconnected and start accepting devices again
                                if(err || complete) {
                                    ptr->free_endpoint();
                                    boost::asio::use_service<usb_acceptor_service>(ptr->m_io_context).pause(false);
                                }
                            });
                        });
                },
#endif
#ifdef MOBSYA_TDM_ENABLE_SERIAL
                [&ptr, id, &firmware, &cb](usb_serial_port& serial) {
                    // Ignore new device during the update
                    boost::asio::use_service<serial_acceptor_service>(ptr->m_io_context).pause(true);
                    boost::system::error_code ec;
                    serial.close(ec);
                    mobsya::upgrade_thymio2_endpoint(
                        serial.device_path(), firmware, id,
                        [ptr, cb](boost::system::error_code err, double progress, bool complete) {
                            // Make sure we are running in our executor
                            boost::asio::post(ptr->m_strand, [cb, ptr, err, progress, complete]() {
                                cb(err, progress, complete);
                                // mark the associated device path as unconnected and start accepting devices again
                                if(err || complete) {
                                    ptr->free_endpoint();
                                    boost::asio::use_service<serial_acceptor_service>(ptr->m_io_context).pause(false);
                                }
                            });
                        });
                },
#endif
                // can never happen
                [](tcp_socket& underlying) {}},

            ptr->m_endpoint);
    });


    return true;
}


}  // namespace mobsya