#pragma once

#include "envoy/network/udp_packet_writer_config.h"
#include "envoy/network/udp_packet_writer_handler.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Network {

class UdpDefaultWriterFactory : public Network::UdpPacketWriterFactory {
public:
  Network::UdpPacketWriterPtr createUdpPacketWriter(Network::IoHandle& io_handle) override;
};

// This class uses a protobuf config to create a UDP packet writer factory which
// creates a UdpPacketWriter. This is the default UDP packet writer if not specified
// in config.
class UdpDefaultWriterConfigFactory : public UdpPacketWriterConfigFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  Network::UdpPacketWriterFactoryPtr
  createUdpPacketWriterFactory(const Protobuf::Message&) override;

  std::string name() const override;
};

DECLARE_FACTORY(UdpDefaultWriterConfigFactory);

} // namespace Network
} // namespace Envoy
