#pragma once

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/quic_packet_writer.h"

#pragma GCC diagnostic pop

#include "envoy/network/udp_packet_writer_handler.h"

namespace Envoy {
namespace Quic {

class QuicEnvoyPacketWriter : public quic::QuicPacketWriter {
public:
  QuicEnvoyPacketWriter(Network::UdpPacketWriterPtr& envoy_udp_packet_writer);

  quic::WriteResult WritePacket(const char* buffer, size_t buf_len,
                                const quic::QuicIpAddress& self_address,
                                const quic::QuicSocketAddress& peer_address,
                                quic::PerPacketOptions* options) override;

  // quic::QuicPacketWriter
  bool IsWriteBlocked() const override { return envoy_udp_packet_writer_->isWriteBlocked(); }
  void SetWritable() override { envoy_udp_packet_writer_->setWritable(); }
  bool IsBatchMode() const override { return envoy_udp_packet_writer_->isBatchMode(); }
  // Currently this writer doesn't support pacing offload.
  bool SupportsReleaseTime() const override { return false; }

  quic::QuicByteCount GetMaxPacketSize(const quic::QuicSocketAddress& peer_address) const override;
  quic::QuicPacketBuffer GetNextWriteLocation(const quic::QuicIpAddress& self_address,
                                              const quic::QuicSocketAddress& peer_address) override;
  quic::WriteResult Flush() override;

private:
  // TODO(yugant): Use UdpPacketWriter& instance, no need to have unique_ptr here
  Network::UdpPacketWriterPtr& envoy_udp_packet_writer_;
};

} // namespace Quic
} // namespace Envoy