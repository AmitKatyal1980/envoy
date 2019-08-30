#pragma once

#include <atomic>
#include <unordered_map>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/async_client.h"
#include "envoy/http/header_map.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"

#include "library/common/types/c_types.h"

namespace Envoy {
namespace Http {

/**
 * Manages HTTP streams, and provides an interface to interact with them.
 * The Dispatcher executes all stream operations on the provided Event::Dispatcher's event loop.
 */
class Dispatcher : public Logger::Loggable<Logger::Id::http> {
public:
  Dispatcher(Event::Dispatcher& event_dispatcher, Upstream::ClusterManager& cluster_manager);

  /**
   * Attempts to open a new stream to the remote. Note that this function is asynchronous and
   * opening a stream may fail. The returned handle is immediately valid for use with this API, but
   * there is no guarantee it will ever functionally represent an open stream.
   * @param observer wrapper for callbacks for events on this stream.
   * @return envoy_stream_t handle to the stream being created.
   */
  envoy_status_t startStream(envoy_stream_t stream, envoy_observer observer);
  envoy_status_t sendHeaders(envoy_stream_t stream, envoy_headers headers, bool end_stream);
  envoy_status_t sendData(envoy_stream_t stream, envoy_data data, bool end_stream);
  envoy_status_t sendMetadata(envoy_stream_t stream, envoy_headers headers, bool end_stream);
  envoy_status_t sendTrailers(envoy_stream_t stream, envoy_headers headers);
  envoy_status_t resetStream(envoy_stream_t stream);

private:
  /**
   * Notifies caller of async HTTP stream status.
   * Note the HTTP stream is full-duplex, even if the local to remote stream has been ended
   * by sendHeaders/sendData with end_stream=true, sendTrailers, or locallyCloseStream
   * DirectStreamCallbacks can continue to receive events until the remote to local stream is
   * closed, or resetStream is called.
   */
  class DirectStreamCallbacks : public AsyncClient::StreamCallbacks,
                                public Logger::Loggable<Logger::Id::http> {
  public:
    DirectStreamCallbacks(envoy_stream_t stream_handle, envoy_observer observer,
                          Dispatcher& http_dispatcher);

    // AsyncClient::StreamCallbacks
    void onHeaders(HeaderMapPtr&& headers, bool end_stream) override;
    void onData(Buffer::Instance& data, bool end_stream) override;
    void onTrailers(HeaderMapPtr&& trailers) override;
    void onComplete() override;
    void onReset() override;

  private:
    const envoy_stream_t stream_handle_;
    const envoy_observer observer_;
    Dispatcher& http_dispatcher_;
  };

  using DirectStreamCallbacksPtr = std::unique_ptr<DirectStreamCallbacks>;

  /**
   * Contains state about an HTTP stream; both in the outgoing direction via an underlying
   * AsyncClient::Stream and in the incoming direction via DirectStreamCallbacks.
   */
  class DirectStream {
  public:
    DirectStream(envoy_stream_t stream_handle, AsyncClient::Stream& underlying_stream,
                 DirectStreamCallbacksPtr&& callbacks);

    const envoy_stream_t stream_handle_;
    // Used to issue outgoing HTTP stream operations.
    AsyncClient::Stream& underlying_stream_;
    // Used to receive incoming HTTP stream operations.
    const DirectStreamCallbacksPtr callbacks_;

    HeaderMapPtr headers_;
    // TODO: because the client may send infinite metadata frames we need some ongoing way to
    // free metadata ahead of object destruction.
    // An implementation option would be to have drainable header maps, or done callbacks.
    std::vector<HeaderMapPtr> metadata_;
    HeaderMapPtr trailers_;
  };

  using DirectStreamPtr = std::unique_ptr<DirectStream>;

  // Everything in the below interface must only be accessed from the event_dispatcher's thread.
  // This allows us to generally avoid synchronization.
  DirectStream* getStream(envoy_stream_t stream_handle);
  void cleanup(envoy_stream_t stream_handle);

  std::unordered_map<envoy_stream_t, DirectStreamPtr> streams_;
  // The event_dispatcher is the only member state that may be accessed from a thread other than
  // the event_dispatcher's own thread.
  Event::Dispatcher& event_dispatcher_;
  Upstream::ClusterManager& cluster_manager_;
};

} // namespace Http
} // namespace Envoy