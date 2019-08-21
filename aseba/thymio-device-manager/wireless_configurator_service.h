#pragma once
#include <boost/asio/io_service.hpp>
#include <vector>
#include <range/v3/span.hpp>

#include "log.h"
#include "aseba_node.h"
#include "aseba_node_registery.h"

namespace mobsya {


class wireless_configurator_service : public boost::asio::detail::service_base<wireless_configurator_service>,
                                      public node_status_monitor {

public:
    wireless_configurator_service(boost::asio::execution_context& ctx);
    ~wireless_configurator_service() override;

    bool is_enabled() const {
        return m_enabled;
    }

    void enable();
    void register_configurable_dongle();

protected:
    void node_changed(std::shared_ptr<aseba_node>, const aseba_node_registery::node_id&, aseba_node::status) override;

private:
    bool m_enabled = false;
    void disconnect_all_nodes();
    boost::asio::execution_context& m_ctx;
};


}  // namespace mobsya
