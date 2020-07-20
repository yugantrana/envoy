#pragma once

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
// QUICHE doesn't mark override at QuicBatchWriterBase::SupportsReleaseTime()
#pragma GCC diagnostic ignored "-Winconsistent-missing-override"
// QUICHE allows ignored qualifiers
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "quiche/quic/core/batch_writer/quic_gso_batch_writer.h"

#pragma GCC diagnostic pop

#include "envoy/config/listener/v3/udp_writer_config.pb.h"
#include "envoy/network/udp_packet_writer_handler.h"

#include "common/protobuf/utility.h"
#include "common/runtime/runtime_protos.h"

namespace Envoy {
namespace Quic {

const std::string GsoBatchWriterName{"udp_gso_batch_writer"};

class UdpGsoBatchWriter : public quic::QuicGsoBatchWriter, public Network::UdpPacketWriter {
public:
  UdpGsoBatchWriter(Network::IoHandle& io_handle);

  ~UdpGsoBatchWriter() override;

  // writePacket perform batched sends based on QuicGsoBatchWriter::WritePacket
  // implementation
  Api::IoCallUint64Result writePacket(const Buffer::Instance& buffer,
                                      const Network::Address::Ip* local_ip,
                                      const Network::Address::Instance& peer_address) override;

  bool isWriteBlocked() const override { return IsWriteBlocked(); }
  void setWritable() override { return SetWritable(); }
  bool isBatchMode() const override { return IsBatchMode(); }

  uint64_t getMaxPacketSize(const Network::Address::Instance& peer_address) const override;
  char* getNextWriteLocation(const Network::Address::Ip* local_ip,
                             const Network::Address::Instance& peer_address) override;
  Api::IoCallUint64Result flush() override;

  std::string name() const override { return GsoBatchWriterName; }
  Network::IoHandle& getWriterIoHandle const override;

private:
  Network::IoHandle& io_handle_;
};

class UdpGsoBatchWriterFactory : public Network::UdpPacketWriterFactory {
public:
  UdpGsoBatchWriterFactory();

  Network::UdpPacketWriterPtr createUdpPacketWriter(Network::IoHandle& io_handle);

private:
  envoy::config::core::v3::RuntimeFeatureFlag enabled_;
};

} // namespace Quic
} // namespace Envoy