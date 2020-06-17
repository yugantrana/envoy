#include <netinet/in.h>
#include <sys/socket.h>

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/network/address_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/socket_option_impl.h"
#include "common/network/udp_listener_impl.h"
#include "common/network/utility.h"

#include "test/common/network/listener_impl_test_base.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Network {
namespace {

class UdpListenerImplTest : public ListenerImplTestBase {
public:
  UdpListenerImplTest()
      : server_socket_(createServerSocket(true)), send_to_addr_(getServerLoopbackAddress()) {
    time_system_.advanceTimeWait(std::chrono::milliseconds(100));
  }

  void SetUp() override {
    // Set listening socket options.
    server_socket_->addOptions(SocketOptionFactory::buildIpPacketInfoOptions());
    server_socket_->addOptions(SocketOptionFactory::buildRxQueueOverFlowOptions());
    server_socket_->addOptions(SocketOptionFactory::buildUdpGroOptions());

    listener_ = std::make_unique<UdpListenerImpl>(
        dispatcherImpl(), server_socket_, listener_callbacks_, dispatcherImpl().timeSource());
  }

protected:
  Address::Instance* getServerLoopbackAddress() {
    if (version_ == Address::IpVersion::v4) {
      return new Address::Ipv4Instance(Network::Test::getLoopbackAddressString(version_),
                                       server_socket_->localAddress()->ip()->port());
    }
    return new Address::Ipv6Instance(Network::Test::getLoopbackAddressString(version_),
                                     server_socket_->localAddress()->ip()->port());
  }

  Address::Instance* getServerAnyAddress() {
    if (version_ == Address::IpVersion::v4) {
      return new Address::Ipv4Instance(Network::Test::getAnyAddressString(version_),
                                       server_socket_->localAddress()->ip()->port());
    }
    return new Address::Ipv6Instance(Network::Test::getAnyAddressString(version_),
                                     server_socket_->localAddress()->ip()->port());
  }

  SocketSharedPtr createServerSocket(bool bind) {
    // Set IP_FREEBIND to allow sendmsg to send with non-local IPv6 source address.
    return std::make_shared<UdpListenSocket>(Network::Test::getAnyAddress(version_),
#ifdef IP_FREEBIND
                                             SocketOptionFactory::buildIpFreebindOptions(),
#else
                                             nullptr,
#endif
                                             bind);
  }

  // Validates receive data, source/destination address and received time.
  void validateRecvCallbackParams(const UdpRecvData& data) {
    ASSERT_NE(data.addresses_.local_, nullptr);

    ASSERT_NE(data.addresses_.peer_, nullptr);
    ASSERT_NE(data.addresses_.peer_->ip(), nullptr);

    EXPECT_EQ(data.addresses_.local_->asString(), send_to_addr_->asString());

    EXPECT_EQ(data.addresses_.peer_->ip()->addressAsString(),
              client_.localAddress()->ip()->addressAsString());

    EXPECT_EQ(*data.addresses_.local_, *send_to_addr_);

    size_t num_packet_per_recv = 1u;

    if (Api::OsSysCallsSingleton::get().supportsUdpGro()) {
      // This should be as per packets received
      std::cout << "YUGANT UDPGRO is true" << std::endl;
      num_packet_per_recv = 16u;
    } else if (Api::OsSysCallsSingleton::get().supportsMmsg()) {
      std::cout << "YUGANT MMSG is true" << std::endl;
      num_packet_per_recv = 16u;
    }

    EXPECT_EQ(time_system_.monotonicTime(),
              data.receive_time_ +
                  std::chrono::milliseconds(
                      (num_packets_received_by_listener_ % num_packet_per_recv) * 100));
    // Advance time so that next onData() should have different received time.
    time_system_.advanceTimeWait(std::chrono::milliseconds(100));
    ++num_packets_received_by_listener_;
  }

  SocketSharedPtr server_socket_;
  Network::Test::UdpSyncPeer client_{GetParam()};
  Address::InstanceConstSharedPtr send_to_addr_;
  MockUdpListenerCallbacks listener_callbacks_;
  std::unique_ptr<UdpListenerImpl> listener_;
  size_t num_packets_received_by_listener_{0};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, UdpListenerImplTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test that socket options are set after the listener is setup.
TEST_P(UdpListenerImplTest, UdpSetListeningSocketOptionsSuccess) {
  MockUdpListenerCallbacks listener_callbacks;
  auto socket = std::make_shared<Network::UdpListenSocket>(Network::Test::getAnyAddress(version_),
                                                           nullptr, true);
  std::shared_ptr<MockSocketOption> option = std::make_shared<MockSocketOption>();
  socket->addOption(option);
  EXPECT_CALL(*option, setOption(_, envoy::config::core::v3::SocketOption::STATE_BOUND))
      .WillOnce(Return(true));
  UdpListenerImpl listener(dispatcherImpl(), socket, listener_callbacks,
                           dispatcherImpl().timeSource());

#ifdef SO_RXQ_OVFL
  // Verify that overflow detection is enabled.
  int get_overflow = 0;
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  socklen_t int_size = static_cast<socklen_t>(sizeof(get_overflow));
  const Api::SysCallIntResult result = os_syscalls.getsockopt(
      server_socket_->ioHandle().fd(), SOL_SOCKET, SO_RXQ_OVFL, &get_overflow, &int_size);
  EXPECT_EQ(0, result.rc_);
  EXPECT_EQ(1, get_overflow);
#endif
}

/**
 * Tests UDP listener for actual destination and data.
 */
TEST_P(UdpListenerImplTest, UseActualDstUdp) {
  // We send 2 packets
  const std::string first("first");
  client_.write(first, *send_to_addr_);
  const std::string second("second");
  client_.write(second, *send_to_addr_);

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onData(_))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data);

        EXPECT_EQ(data.buffer_->toString(), first);
      }))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data);

        EXPECT_EQ(data.buffer_->toString(), second);

        dispatcher_->exit();
      }));

  EXPECT_CALL(listener_callbacks_, onWriteReady(_))
      .WillRepeatedly(Invoke([&](const Socket& socket) {
        EXPECT_EQ(socket.ioHandle().fd(), server_socket_->ioHandle().fd());
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener for read and write callbacks with actual data.
 */
TEST_P(UdpListenerImplTest, UdpEcho) {
  // We send 17 packets and expect it to echo.
  absl::FixedArray<std::string> client_data({"first", "second", "third", "forth", "fifth", "sixth",
                                             "seventh", "eighth", "ninth", "tenth", "eleventh",
                                             "twelveth", "thirteenth", "fourteenth", "fifteenth",
                                             "sixteenth", "seventeenth"});
  for (const auto& i : client_data) {
    client_.write(i, *send_to_addr_);
  }

  // For unit test purposes, we assume that the data was received in order.
  Address::InstanceConstSharedPtr test_peer_address;

  std::vector<std::string> server_received_data;

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onData(_))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data);

        test_peer_address = data.addresses_.peer_;

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, client_data[num_packets_received_by_listener_ - 1]);

        server_received_data.push_back(data_str);
      }))
      .WillRepeatedly(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data);

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, client_data[num_packets_received_by_listener_ - 1]);

        server_received_data.push_back(data_str);
      }));

  EXPECT_CALL(listener_callbacks_, onWriteReady(_)).WillOnce(Invoke([&](const Socket& socket) {
    EXPECT_EQ(socket.ioHandle().fd(), server_socket_->ioHandle().fd());
    ASSERT_NE(test_peer_address, nullptr);

    for (const auto& data : server_received_data) {
      const std::string::size_type data_size = data.length() + 1;
      uint64_t total_sent = 0;
      const void* void_data = static_cast<const void*>(data.c_str() + total_sent);
      Buffer::RawSlice slice{const_cast<void*>(void_data), data_size - total_sent};

      Api::IoCallUint64Result send_rc = Api::ioCallUint64ResultNoError();
      do {
        send_rc = Network::Utility::writeToSocket(const_cast<Socket*>(&socket)->ioHandle(), &slice,
                                                  1, nullptr, *test_peer_address);

        if (send_rc.ok()) {
          total_sent += send_rc.rc_;
          if (total_sent >= data_size) {
            break;
          }
        } else if (send_rc.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
          break;
        }
      } while (((send_rc.rc_ == 0) &&
                (send_rc.err_->getErrorCode() == Api::IoError::IoErrorCode::Again)) ||
               (total_sent < data_size));

      EXPECT_EQ(total_sent, data_size);
    }

    server_received_data.clear();
    dispatcher_->exit();
  }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener's `enable` and `disable` APIs.
 */
TEST_P(UdpListenerImplTest, UdpListenerEnableDisable) {
  auto const* server_ip = server_socket_->localAddress()->ip();
  ASSERT_NE(server_ip, nullptr);

  // We first disable the listener and then send two packets.
  // - With the listener disabled, we expect that none of the callbacks will be
  // called.
  // - When the listener is enabled back, we expect the callbacks to be called
  listener_->disable();
  const std::string first("first");
  client_.write(first, *send_to_addr_);
  const std::string second("second");
  client_.write(second, *send_to_addr_);

  EXPECT_CALL(listener_callbacks_, onReadReady()).Times(0);
  EXPECT_CALL(listener_callbacks_, onData(_)).Times(0);

  EXPECT_CALL(listener_callbacks_, onWriteReady(_)).Times(0);

  dispatcher_->run(Event::Dispatcher::RunType::Block);

  listener_->enable();

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onData(_))
      .Times(2)
      .WillOnce(Return())
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data);

        EXPECT_EQ(data.buffer_->toString(), second);

        dispatcher_->exit();
      }));

  EXPECT_CALL(listener_callbacks_, onWriteReady(_))
      .WillRepeatedly(Invoke([&](const Socket& socket) {
        EXPECT_EQ(socket.ioHandle().fd(), server_socket_->ioHandle().fd());
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener's error callback.
 */
TEST_P(UdpListenerImplTest, UdpListenerRecvMsgError) {
  auto const* server_ip = server_socket_->localAddress()->ip();
  ASSERT_NE(server_ip, nullptr);

  // When the `receive` system call returns an error, we expect the `onReceiveError`
  // callback called with `SyscallError` parameter.
  const std::string first("first");
  client_.write(first, *send_to_addr_);

  EXPECT_CALL(listener_callbacks_, onData(_)).Times(0);

  EXPECT_CALL(listener_callbacks_, onWriteReady(_)).WillOnce(Invoke([&](const Socket& socket) {
    EXPECT_EQ(socket.ioHandle().fd(), server_socket_->ioHandle().fd());
  }));

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onReceiveError(_))
      .WillOnce(Invoke([&](Api::IoError::IoErrorCode err) -> void {
        ASSERT_EQ(Api::IoError::IoErrorCode::NoSupport, err);

        dispatcher_->exit();
      }));
  // Inject mocked OsSysCalls implementation to mock a read failure.
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, supportsUdpGro());
  EXPECT_CALL(os_sys_calls, supportsMmsg());
  EXPECT_CALL(os_sys_calls, recvmsg(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{-1, ENOTSUP}));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener for sending datagrams to destination.
 *  1. Setup a udp listener and client socket
 *  2. Send the data from the udp listener to the client socket and validate the contents and source
 * address.
 */
TEST_P(UdpListenerImplTest, SendData) {
  const std::string payload("hello world");
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  buffer->add(payload);
  // Use a self address that is unlikely to be picked by source address discovery
  // algorithm if not specified in recvmsg/recvmmsg. Port is not taken into
  // consideration.
  Address::InstanceConstSharedPtr send_from_addr;
  if (version_ == Address::IpVersion::v4) {
    // Linux kernel regards any 127.x.x.x as local address. But Mac OS doesn't.
    send_from_addr = std::make_shared<Address::Ipv4Instance>(
#ifndef __APPLE__
        "127.1.2.3",
#else
        "127.0.0.1",
#endif
        server_socket_->localAddress()->ip()->port());
  } else {
    // Only use non-local v6 address if IP_FREEBIND is supported. Otherwise use
    // ::1 to avoid EINVAL error. Unfortunately this can't verify that sendmsg with
    // customized source address is doing the work because kernel also picks ::1
    // if it's not specified in cmsghdr.
    send_from_addr = std::make_shared<Address::Ipv6Instance>(
#ifdef IP_FREEBIND
        "::9",
#else
        "::1",
#endif
        server_socket_->localAddress()->ip()->port());
  }

  UdpSendData send_data{send_from_addr->ip(), *client_.localAddress(), *buffer};

  auto send_result = listener_->send(send_data);

  EXPECT_TRUE(send_result.ok()) << "send() failed : " << send_result.err_->getErrorDetails();

  const uint64_t bytes_to_read = payload.length();
  UdpRecvData data;
  client_.recv(data);
  EXPECT_EQ(bytes_to_read, data.buffer_->length());
  EXPECT_EQ(send_from_addr->asString(), data.addresses_.peer_->asString());
  EXPECT_EQ(data.buffer_->toString(), payload);
}

/**
 * The send fails because the server_socket is created with bind=false.
 */
TEST_P(UdpListenerImplTest, SendDataError) {
  Logger::StderrSinkDelegate stderr_sink(Logger::Registry::getSink()); // For coverage build.
  const std::string payload("hello world");
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  buffer->add(payload);
  // send data to itself
  UdpSendData send_data{send_to_addr_->ip(), *server_socket_->localAddress(), *buffer};

  // Inject mocked OsSysCalls implementation to mock a write failure.
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, sendmsg(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{-1, ENOTSUP}));
  auto send_result = listener_->send(send_data);
  EXPECT_FALSE(send_result.ok());
  EXPECT_EQ(send_result.err_->getErrorCode(), Api::IoError::IoErrorCode::NoSupport);
  // Failed write shouldn't drain the data.
  EXPECT_EQ(payload.length(), buffer->length());

  ON_CALL(os_sys_calls, sendmsg(_, _, _)).WillByDefault(Return(Api::SysCallSizeResult{-1, EINVAL}));
  // EINVAL should cause RELEASE_ASSERT.
  EXPECT_DEATH(listener_->send(send_data), "Invalid argument passed in");
}

/**
 * Enable UDP_GRO and test if the packets received are handled correctly
 */
TEST_P(UdpListenerImplTest, UdpGroBasic) {
  // We send 17 packets and expect it to echo.
  absl::FixedArray<std::string> client_data({"first", "secon", "third"});

  for (const auto& i : client_data) {
    client_.write(i, *send_to_addr_);
  }

  // For unit test purposes, we assume that the data was received in order.
  Address::InstanceConstSharedPtr test_peer_address;

  std::vector<std::string> server_received_data;

  std::cout << "YUGANT:: GroBasic is running" << std::endl;

  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  // Set supportsUdpGro to be set as True for all returns.
  EXPECT_CALL(os_sys_calls, supportsUdpGro).WillRepeatedly(Return(true));
  EXPECT_CALL(os_sys_calls, supportsMmsg).Times(0);

  EXPECT_CALL(os_sys_calls, recvmsg(_, _, _)).WillRepeatedly(Invoke([](os_fd_t, msghdr* msg, int) {
    std::cout << "YUGANT: Mocked Recvmsg Called" << std::endl;
    std::cout << "YUGANT: msg_iovlen is: " << msg->msg_iovlen << std::endl;
    std::cout << "YUGANT: msg_controllen is: " << msg->msg_controllen << std::endl;

    // Set msg_name and msg_namelen
    sockaddr_storage ss;
    auto ipv4_addr = reinterpret_cast<sockaddr_in*>(&ss);
    memset(ipv4_addr, 0, sizeof(sockaddr_in));
    ipv4_addr->sin_family = AF_INET;
    ipv4_addr->sin_addr.s_addr = INADDR_LOOPBACK;
    ipv4_addr->sin_port = htons(54321);
    *reinterpret_cast<sockaddr_in*>(msg->msg_name) = *ipv4_addr;
    msg->msg_namelen = sizeof(sockaddr_in);

    // Set the iovecs to carry the data
    const uint64_t size_of_payload = 4500;
    auto slice = Buffer::OwnedSlice::create(size_of_payload);
    EXPECT_NE(nullptr, slice->data());
    EXPECT_EQ(0, slice->dataSize());
    EXPECT_LE(size_of_payload, slice->reservableSize());

    absl::FixedArray<iovec> iov(msg->msg_iovlen);
    iov[0].iov_base = slice->data();
    iov[0].iov_len = slice->dataSize();
    msg->msg_iov = iov.begin();

    // Populate cbuf
    const size_t cmsg_space = CMSG_SPACE(msg->msg_controllen);
    std::cout << "YUGANT: cmsg_space is: " << cmsg_space << std::endl;

    absl::FixedArray<char> cbuf(cmsg_space);
    memset(cbuf.begin(), 0, cmsg_space);
    msg->msg_control = cbuf.begin();
    msg->msg_controllen = cmsg_space * sizeof(char);
    cmsghdr* cmsg = CMSG_FIRSTHDR(msg);

    // cmsg->cmsg_level = IPPROTO_IP;
    // #ifndef IP_SENDSRCADDR
    // cmsg->cmsg_len = CMSG_LEN(sizeof(in_pktinfo));
    // cmsg->cmsg_type = IP_PKTINFO;
    // auto pktinfo = reinterpret_cast<in_pktinfo*>(CMSG_DATA(cmsg));
    // pktinfo->ipi_ifindex = 0;
    // pktinfo->ipi_spec_dst.s_addr = self_ip->ipv4()->address();
    // #else
    // cmsg->cmsg_type = IP_SENDSRCADDR;
    // cmsg->cmsg_len = CMSG_LEN(sizeof(in_addr));
    // *(reinterpret_cast<struct in_addr*>(CMSG_DATA(cmsg))).s_addr = self_ip->ipv4()->address();
    // #endif
    // cmsg = CMSG_NXTHDR(msg, cmsg);

    cmsg->cmsg_level = SOL_UDP;
    cmsg->cmsg_type = GRO_UDP;
    cmsg->cmsg_len = CMSG_LEN(sizeof(uint16_t));
    const uint64_t gso_size = 1500;
    *reinterpret_cast<uint16_t*>(CMSG_DATA(cmsg)) = gso_size;

    return Api::SysCallSizeResult{4500u, 0};
  }));

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onData(_))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data);

        test_peer_address = data.addresses_.peer_;

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, client_data[num_packets_received_by_listener_ - 1]);

        server_received_data.push_back(data_str);
      }))
      .WillRepeatedly(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data);

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, client_data[num_packets_received_by_listener_ - 1]);

        server_received_data.push_back(data_str);
      }));

  EXPECT_CALL(listener_callbacks_, onWriteReady(_)).WillOnce(Invoke([&](const Socket& socket) {
    EXPECT_EQ(socket.ioHandle().fd(), server_socket_->ioHandle().fd());
    ASSERT_NE(test_peer_address, nullptr);

    for (const auto& data : server_received_data) {
      const std::string::size_type data_size = data.length() + 1;
      uint64_t total_sent = 0;
      const void* void_data = static_cast<const void*>(data.c_str() + total_sent);
      Buffer::RawSlice slice{const_cast<void*>(void_data), data_size - total_sent};

      Api::IoCallUint64Result send_rc = Api::ioCallUint64ResultNoError();
      do {
        send_rc = Network::Utility::writeToSocket(const_cast<Socket*>(&socket)->ioHandle(), &slice,
                                                  1, nullptr, *test_peer_address);

        if (send_rc.ok()) {
          total_sent += send_rc.rc_;
          if (total_sent >= data_size) {
            break;
          }
        } else if (send_rc.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
          break;
        }
      } while (((send_rc.rc_ == 0) &&
                (send_rc.err_->getErrorCode() == Api::IoError::IoErrorCode::Again)) ||
               (total_sent < data_size));

      EXPECT_EQ(total_sent, data_size);
    }

    server_received_data.clear();
    dispatcher_->exit();
  }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

} // namespace
} // namespace Network
} // namespace Envoy