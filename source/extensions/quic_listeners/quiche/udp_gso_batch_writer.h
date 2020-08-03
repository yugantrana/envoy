#pragma once

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
// QUICHE allows ignored qualifiers
#pragma GCC diagnostic ignored "-Wignored-qualifiers"

// QUICHE doesn't mark override at QuicBatchWriterBase::SupportsReleaseTime()
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#elif defined(__GNUC__) && __GNUC__ >= 5
#pragma GCC diagnostic ignored "-Wsuggest-override"
#endif

#include "quiche/quic/core/batch_writer/quic_gso_batch_writer.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#pragma GCC diagnostic pop

#include "envoy/network/udp_packet_writer_handler.h"

#include "common/protobuf/utility.h"
#include "common/runtime/runtime_protos.h"

namespace Envoy {
namespace Quic {

#define UDP_GSO_BATCH_WRITER_STATS(COUNTER, GAUGE, HISTOGRAM)                                      \
  COUNTER(total_bytes_sent)                                                                        \
  GAUGE(internal_buffer_size, NeverImport)                                                         \
  GAUGE(front_buffered_pkt_size, NeverImport)                                                      \
  HISTOGRAM(pkts_sent_per_batch, Unspecified)

/**
 * Wrapper struct for udp gso batch writer stats. @see stats_macros.h
 */
struct UdpGsoBatchWriterStats {
  UDP_GSO_BATCH_WRITER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                             GENERATE_HISTOGRAM_STRUCT)
};

/**
 * UdpPacketWriter implementation based on quic::QuicGsoBatchWriter to send packets
 * in batches, using UDP socket's generic segmentation offload(GSO) capability.
 */
class UdpGsoBatchWriter : public quic::QuicGsoBatchWriter, public Network::UdpPacketWriter {
public:
  UdpGsoBatchWriter(Network::IoHandle& io_handle, Stats::Scope& scope);

  ~UdpGsoBatchWriter() override;

  // writePacket perform batched sends based on QuicGsoBatchWriter::WritePacket
  Api::IoCallUint64Result writePacket(const Buffer::Instance& buffer,
                                      const Network::Address::Ip* local_ip,
                                      const Network::Address::Instance& peer_address) override;

  // UdpPacketWriter Implementations
  bool isWriteBlocked() const override { return IsWriteBlocked(); }
  void setWritable() override { return SetWritable(); }
  bool isBatchMode() const override { return IsBatchMode(); }
  uint64_t getMaxPacketSize(const Network::Address::Instance& peer_address) const override;
  Network::UdpPacketWriterBuffer
  getNextWriteLocation(const Network::Address::Ip* local_ip,
                       const Network::Address::Instance& peer_address) override;
  Api::IoCallUint64Result flush() override;

  /**
   * @brief Update stats_ field for the udp packet writer
   * @param quic_result is the result from Flush/WritePacket
   */
  void updateUdpGsoBatchWriterStats(quic::WriteResult quic_result);

  /**
   * @brief Generate UdpGsoBatchWriterStats object from scope
   * @param scope for stats
   * @return UdpGsoBatchWriterStats for scope
   */
  UdpGsoBatchWriterStats generateStats(Stats::Scope& scope);

  // Override Quic WritePacket
  quic::WriteResult WritePacket(const char* buffer, size_t buf_len,
                                const quic::QuicIpAddress& self_address,
                                const quic::QuicSocketAddress& peer_address,
                                quic::PerPacketOptions* options) override;

private:
  UdpGsoBatchWriterStats stats_;
};

class UdpGsoBatchWriterFactory : public Network::UdpPacketWriterFactory {
public:
  UdpGsoBatchWriterFactory();

  Network::UdpPacketWriterPtr createUdpPacketWriter(Network::IoHandle& io_handle,
                                                    Stats::Scope& scope) override;

private:
  envoy::config::core::v3::RuntimeFeatureFlag enabled_;
};

} // namespace Quic
} // namespace Envoy
