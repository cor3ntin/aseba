#include "wireless_configurator_service.h"
#include <range/v3/algorithm.hpp>
#include "aseba_node_registery.h"

namespace mobsya {


wireless_configurator_service::wireless_configurator_service(boost::asio::execution_context& ctx)
    : boost::asio::detail::service_base<wireless_configurator_service>(static_cast<boost::asio::io_context&>(ctx))
    , m_ctx(ctx) {}

wireless_configurator_service::~wireless_configurator_service() {}

void wireless_configurator_service::enable() {
    m_enabled = true;
    disconnect_all_nodes();
    // start_node_monitoring(boost::asio::use_service<aseba_node_registery>(m_ctx));
}

void wireless_configurator_service::disconnect_all_nodes() {
    auto& service = boost::asio::use_service<aseba_node_registery>(this->m_ctx);
    service.disconnect_all_wireless_endpoints();
}

void wireless_configurator_service::node_changed(std::shared_ptr<aseba_node> node, const aseba_node_registery::node_id&,
                                                 aseba_node::status status) {}

}  // namespace mobsya
