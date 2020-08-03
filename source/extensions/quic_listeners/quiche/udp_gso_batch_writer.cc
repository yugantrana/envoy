#include "extensions/quic_listeners/quiche/udp_gso_batch_writer.h"

#include "common/network/io_socket_error_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {
namespace {
Api::IoCallUint64Result convertQuicWriteResult(quic::WriteResult quic_result, size_t payload_len) {
  switch (quic_result.status) {
  case quic::WRITE_STATUS_OK: {
    if (quic_result.bytes_written == 0) {
      ENVOY_LOG_MISC(trace, "sendmsg successful, message buffered to send");
    } else {
      ENVOY_LOG_MISC(trace, "sendmsg successful, flushed bytes {}", quic_result.bytes_written);
    }
    // Return payload_len as rc & nullptr as error on success
    return Api::IoCallUint64Result(
        /*rc=*/payload_len,
        /*err=*/Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError));
  }
  case quic::WRITE_STATUS_BLOCKED_DATA_BUFFERED: {
    // Data was buffered, Return payload_len as rc & nullptr as error
    ENVOY_LOG_MISC(trace, "sendmsg blocked, message buffered to send");
    return Api::IoCallUint64Result(
        /*rc=*/payload_len,
        /*err=*/Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError));
  }
  case quic::WRITE_STATUS_BLOCKED: {
    // Writer blocked, return error
    ENVOY_LOG_MISC(trace, "sendmsg blocked, message not buffered");
    return Api::IoCallUint64Result(
        /*rc=*/0,
        /*err=*/Api::IoErrorPtr(Network::IoSocketError::getIoSocketEagainInstance(),
                                Network::IoSocketError::deleteIoError));
  }
  default: {
    // Write Failed, return {0 and error_code}
    ENVOY_LOG_MISC(trace, "sendmsg failed with error code {}",
                   static_cast<int>(quic_result.error_code));
    return Api::IoCallUint64Result(
        /*rc=*/0,
        /*err=*/Api::IoErrorPtr(new Network::IoSocketError(quic_result.error_code),
                                Network::IoSocketError::deleteIoError));
  }
  }
}

} // namespace

// Initialize QuicGsoBatchWriter, set io_handle_ and stats_
UdpGsoBatchWriter::UdpGsoBatchWriter(Network::IoHandle& io_handle, Stats::Scope& scope)
    : quic::QuicGsoBatchWriter(std::make_unique<quic::QuicBatchWriterBuffer>(), io_handle.fd()),
      stats_(generateStats(scope)) {
  ENVOY_LOG_MISC(trace, "GSO_PERF: GSO Batch writer initialized");
}

// Do Nothing in the Destructor For now
UdpGsoBatchWriter::~UdpGsoBatchWriter() = default;

Api::IoCallUint64Result
UdpGsoBatchWriter::writePacket(const Buffer::Instance& buffer, const Network::Address::Ip* local_ip,
                               const Network::Address::Instance& peer_address) {
  ENVOY_LOG_MISC(trace, "GSO_PERF: GSO Batch writer writePacket called");
  // Convert received parameters to relevant forms
  quic::QuicSocketAddress peer_addr = envoyAddressIpToQuicSocketAddress(peer_address.ip());
  quic::QuicSocketAddress self_addr = envoyAddressIpToQuicSocketAddress(local_ip);
  size_t payload_len = static_cast<size_t>(buffer.length());

  // TODO(yugant): Currently we do not use PerPacketOptions with Quic, we may want to
  // specify this parameter here at a later stage.
  quic::WriteResult quic_result =
      WritePacket(buffer.toString().c_str(), payload_len, self_addr.host(), peer_addr,
                  /*quic::PerPacketOptions=*/nullptr);

  return convertQuicWriteResult(quic_result, payload_len);
}

quic::WriteResult UdpGsoBatchWriter::WritePacket(const char* buffer, size_t buf_len,
                                                 const quic::QuicIpAddress& self_address,
                                                 const quic::QuicSocketAddress& peer_address,
                                                 quic::PerPacketOptions* options) {
  ENVOY_LOG_MISC(trace, "GSO_PERF: Calling the overriden WritePacket in GsoWriter");
  quic::WriteResult quic_result =
      quic::QuicGsoBatchWriter::WritePacket(buffer, buf_len, self_address, peer_address, options);
  ENVOY_LOG_MISC(trace, "GSO_PERF: Completed the Write here!");
  updateUdpGsoBatchWriterStats(quic_result);
  return quic_result;
}

uint64_t UdpGsoBatchWriter::getMaxPacketSize(const Network::Address::Instance& peer_address) const {
  quic::QuicSocketAddress peer_addr = envoyAddressIpToQuicSocketAddress(peer_address.ip());
  return static_cast<uint64_t>(GetMaxPacketSize(peer_addr));
}

Network::UdpPacketWriterBuffer
UdpGsoBatchWriter::getNextWriteLocation(const Network::Address::Ip* local_ip,
                                        const Network::Address::Instance& peer_address) {
  quic::QuicSocketAddress peer_addr = envoyAddressIpToQuicSocketAddress(peer_address.ip());
  quic::QuicSocketAddress self_addr = envoyAddressIpToQuicSocketAddress(local_ip);
  quic::QuicPacketBuffer quic_buf = GetNextWriteLocation(self_addr.host(), peer_addr);
  return Network::UdpPacketWriterBuffer(quic_buf.buffer, quic_buf.release_buffer);
}

Api::IoCallUint64Result UdpGsoBatchWriter::flush() {
  quic::WriteResult quic_result = Flush();
  updateUdpGsoBatchWriterStats(quic_result);

  return convertQuicWriteResult(quic_result, /*payload_len=*/0);
}

void UdpGsoBatchWriter::updateUdpGsoBatchWriterStats(quic::WriteResult quic_result) {
  if (quic_result.status == quic::WRITE_STATUS_OK && quic_result.bytes_written > 0) {
    if (stats_.front_buffered_pkt_size_.value() > 0u) {
      uint64_t num_pkts_in_batch = std::ceil(static_cast<float>(quic_result.bytes_written) /
                                             stats_.front_buffered_pkt_size_.value());
      stats_.pkts_sent_per_batch_.recordValue(num_pkts_in_batch);
    }
    stats_.total_bytes_sent_.add(quic_result.bytes_written);
  }
  stats_.internal_buffer_size_.set(batch_buffer().SizeInUse());
  stats_.front_buffered_pkt_size_.set(
      buffered_writes().empty() ? 0u : buffered_writes().front().buf_len);
}

UdpGsoBatchWriterStats UdpGsoBatchWriter::generateStats(Stats::Scope& scope) {
  return {
      UDP_GSO_BATCH_WRITER_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope), POOL_HISTOGRAM(scope))};
}

UdpGsoBatchWriterFactory::UdpGsoBatchWriterFactory() = default;

Network::UdpPacketWriterPtr
UdpGsoBatchWriterFactory::createUdpPacketWriter(Network::IoHandle& io_handle, Stats::Scope& scope) {
  // Keep It Simple for now
  return std::make_unique<UdpGsoBatchWriter>(io_handle, scope);
}

} // namespace Quic
} // namespace Envoy
