import Foundation

/// Builder used for constructing instances of `ResponseTrailers`.
@objcMembers
public final class ResponseTrailersBuilder: HeadersBuilder {
  /// Initialize a new instance of the builder.
  public convenience init() {
    self.init(headers: [:])
  }

  /// Build the response trailers using the current builder.
  ///
  /// - returns: New instance of response trailers.
  public func build() -> ResponseTrailers {
    return ResponseTrailers(headers: self.headers)
  }
}