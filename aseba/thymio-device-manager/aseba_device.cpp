#include "aseba_device.h"
#ifdef MOBSYA_TDM_ENABLE_SERIAL
#    include "serialacceptor.h"
#endif
#ifdef MOBSYA_TDM_ENABLE_USB
#    include "usbacceptor.h"
#endif
#include "aseba_tcpacceptor.h"

namespace mobsya {


void aseba_device::cancel_all_ops() {
    boost::system::error_code ec;
    variant_ns::visit([&ec](auto& underlying) { return underlying.cancel(ec); }, m_endpoint);
}

aseba_device::~aseba_device() {
    free_endpoint();
}

void aseba_device::free_endpoint() {
    variant_ns::visit(
        overloaded{[this](tcp_socket&) {
                       boost::asio::post(get_io_context(), [this, &ctx = get_io_context()] {
                           boost::asio::use_service<aseba_tcp_acceptor>(ctx).free_endpoint(this);
                       });
                   }
#ifdef MOBSYA_TDM_ENABLE_SERIAL
                   ,
                   [this](mobsya::usb_serial_port& d) {
                       auto timer = std::make_shared<boost::asio::deadline_timer>(get_io_context());
                       timer->expires_from_now(boost::posix_time::milliseconds(500));
                       timer->async_wait([timer, path = d.device_path(), &ctx = get_io_context()](auto ec) {
                           if(!ec)
                               boost::asio::use_service<serial_acceptor_service>(ctx).free_device(path);
                       });
                   }
#endif
#ifdef MOBSYA_TDM_ENABLE_USB
                   ,
                   [this](mobsya::usb_device& d) {
                       if(d.native_handle()) {
                                             boost::asio::post(get_io_context(), [h = libusb_get_device(d.native_handle(),
    &ctx = get_io_context()] { boost::asio::use_service<usb_acceptor_service>(ctx) .free_device(h));
                                             });
                       }
                   }
#endif
        },
        m_endpoint);
}

void aseba_device::stop() {
    variant_ns::visit(overloaded{[](tcp_socket& socket) { socket.cancel(); }
#ifdef MOBSYA_TDM_ENABLE_SERIAL
                                 ,
                                 [](mobsya::usb_serial_port& d) {
                                     boost::system::error_code ec;
                                     d.cancel(ec);
                                 }
#endif
#ifdef MOBSYA_TDM_ENABLE_USB
                                 ,
                                 [this](mobsya::usb_device& d) { d.cancel(); }
#endif
                      },
                      m_endpoint);
}

void aseba_device::close() {
    variant_ns::visit(overloaded{[](tcp_socket&) {}
#ifdef MOBSYA_TDM_ENABLE_SERIAL
                                 ,
                                 [](mobsya::usb_serial_port& d) { d.close(); }
#endif
#ifdef MOBSYA_TDM_ENABLE_USB
                                 ,
                                 [this](mobsya::usb_device& d) {}
#endif
                      },
                      m_endpoint);
}

}  // namespace mobsya
