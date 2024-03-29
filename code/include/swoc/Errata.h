// SPDX-License-Identifier: Apache-2.0
// Copyright Network Geographics 2014
/** @file
    Stacking error message handling.

    The problem addressed by this library is the ability to pass back detailed error messages from
    failures. It is hard to get good diagnostics because the specific failures and general context
    are located in very different stack frames. This library allows local functions to pass back
    local messages which can be easily augmented as the error travels up the stack frame.

    This aims to improve over exceptions by being lower cost and not requiring callers to handle the
    messages. On the other hand, the messages could be used just as easily with exceptions.

    Each message on a stack contains text and a numeric identifier. The identifier value zero is
    reserved for messages that are not errors so that information can be passed back even in the
    success case.

    The implementation takes the position that success must be fast and failure is expensive.
    Therefore Errata is optimized for the success path, imposing very little overhead in that case.
    On the other hand, if an error occurs and is handled, that is generally so expensive that
    optimizations are pointless (although, of course, code should not be gratuitiously expensive).

    The library provides the @c Rv ("return value") template to make returning values and status
    easier. This template allows a function to return a value and status pair with minimal changes.
    The pair acts like the value type in most situations, while providing access to the status.

    Each instance of an erratum is a wrapper class that emulates value semantics (copy on write).
    This means passing even large message stacks is inexpensive, involving only a pointer copy and
    reference counter increment and decrement. A success value is represented by an internal @c NULL
    so it is even cheaper to copy.

    To further ease use, the library has the ability to define @a sinks.  A sink is a function that
    acts on an erratum when it becomes unreferenced. The intended use is to send the messages to an
    output log. This makes reporting errors to a log from even deeply nested functions easy while
    preserving the ability of the top level logic to control such logging.
 */

#pragma once

#include "swoc/swoc_version.h"
#include <vector>
#include <string_view>
#include <functional>
#include <atomic>
#include "swoc/MemArena.h"
#include "swoc/bwf_base.h"
#include "swoc/IntrusiveDList.h"

namespace swoc { inline namespace SWOC_VERSION_NS {
/// Severity levels for Errata.
enum class Severity : uint8_t {
  DIAG,  ///< Diagnostic (internal use).
  INFO,  ///< User visible but not a problem.
  WARN,  ///< Warning.
  ERROR, ///< Error.
};

/** Class to hold a stack of error messages (the "errata"). This is a smart handle class, which
 * wraps the actual data and can therefore be treated a value type with cheap copy semantics.
 * Default construction is very cheap.
 */
class Errata {
public:
  using Severity = swoc::Severity; ///< Import for associated classes.

  /// Severity used if not specified.
  static constexpr Severity DEFAULT_SEVERITY{Severity::DIAG};
  /// Severity level at which the instance is a failure of some sort.
  static constexpr Severity FAILURE_SEVERITY{Severity::WARN};

  /** An annotation to the Errata consisting of a severity and informative text.
   *
   */
  struct Annotation {
    using self_type = Annotation; ///< Self reference type.

    /// Default constructor.
    /// The message has default severity and empty text.
    Annotation();

    /** Construct with severity @a level and @a text.
     *
     * @param severity Severity level.
     * @param text Annotation content (literal).
     */
    Annotation(Severity severity, std::string_view text);

    /// Reset to the message to default state.
    self_type &clear();

    /// Get the severity.
    Severity severity() const;

    /// Get the text of the message.
    std::string_view text() const;

    // Get the nesting level.
    unsigned level() const;

    /// Set the text of the message.
    self_type &assign(std::string_view text);

    /// Set the severity @a level
    self_type &assign(Severity level);

  protected:
    Severity _severity{Errata::DEFAULT_SEVERITY}; ///< Annotation code.
    unsigned _level{0};                           ///< Nesting level.
    std::string_view _text;                       ///< Annotation text.

    /// @{{
    /// Policy and links for intrusive list.
    self_type *_next{nullptr};
    self_type *_prev{nullptr};
    /// @}}
    /// Intrusive list link descriptor.
    /// @note Must explicitly use defaults because ICC and clang consider them inaccessible
    /// otherwise. I consider it a bug in the compiler that a default identical to an explicit
    /// value has different behavior.
    using Linkage = swoc::IntrusiveLinkage<self_type, &self_type::_next, &self_type::_prev>;

    friend class Errata;
  };

protected:
  using self_type = Errata; ///< Self reference type.
  /// Storage type for list of messages.
  /// Internally the vector is accessed backwards, in order to make it LIFO.
  using Container = IntrusiveDList<Annotation::Linkage>;

  /// Implementation class.
  struct Data {
    using self_type = Data; ///< Self reference type.

    /// Construct into @c MemArena.
    Data(swoc::MemArena &&arena);

    /// Check if there are any notes.
    bool empty() const;

    /** Duplicate @a src in the arena for this instance.
     *
     * @param src Source data.
     * @return View of copy in this arena.
     */
    std::string_view localize(std::string_view src);

    /// Get the remnant of the curret block in the arena.
    swoc::MemSpan<char> remnant();

    /// Allocate from the arena.
    swoc::MemSpan<char> alloc(size_t n);

    /// Reference count.
    std::atomic<int> _ref_count{0};

    /// The message stack.
    Container _notes;
    /// Annotation text storage.
    swoc::MemArena _arena;
    /// Nesting level.
    unsigned _level{0};
    /// The effective severity of the message stack.
    Severity _severity{Errata::DEFAULT_SEVERITY};
  };

public:
  /// Default constructor - empty errata, very fast.
  Errata();
  Errata(self_type const &that);                                   ///< Reference counting copy constructor.
  Errata(self_type &&that);                                        ///< Move constructor.
  self_type &operator=(self_type const &that) = delete;            // no copy assignemnt.
  self_type &operator                         =(self_type &&that); ///< Move assignment.
  ~Errata();                                                       ///< Destructor.

  /** Add a new message to the top of stack with severity @a level and @a text.
   * @param severity Severity of the message.
   * @param text Text of the message.
   * @return *this
   */
  self_type &note(Severity severity, std::string_view text);

  /** Push a constructed @c Annotation.
      The @c Annotation is set to have the @a id and @a code. The other arguments are converted
      to strings and concatenated to form the messsage text.
      @return A reference to this object.
  */
  template <typename... Args> self_type &note(Severity severity, std::string_view fmt, Args &&... args);

  /** Push a constructed @c Annotation.
      The @c Annotation is set to have the @a id and @a code. The other arguments are converted
      to strings and concatenated to form the messsage text.
      @return A reference to this object.
  */
  template <typename... Args> self_type &note_v(Severity severity, std::string_view fmt, std::tuple<Args...> const &args);

  /** Copy messages from @a that to @a this.
   *
   * @param that Source object from which to copy.
   * @return @a *this
   */
  self_type &note(self_type const &that);

  /** Copy messages from @a that to @a this, then clear @a that.
   *
   * @param that Source object from which to copy.
   * @return @a *this
   */
  self_type &note(self_type &&that);

  /// Overload for @c DIAG severity notes.
  template <typename... Args> self_type &diag(std::string_view fmt, Args &&... args) &;
  template <typename... Args> self_type diag(std::string_view fmt, Args &&... args) &&;
  /// Overload for @c INFO severity notes.
  template <typename... Args> self_type &info(std::string_view fmt, Args &&... args)&;
  template <typename... Args> self_type info(std::string_view fmt, Args &&... args) &&;
  /// Overload for @c WARN severity notes.
  template <typename... Args> self_type &warn(std::string_view fmt, Args &&... args) &;
  template <typename... Args> self_type warn(std::string_view fmt, Args &&... args) &&;
  /// Overload for @c ERROR severity notes.
  template <typename... Args> self_type &error(std::string_view fmt, Args &&... args) &;
  template <typename... Args> self_type error(std::string_view fmt, Args &&... args) &&;

  /// Remove all messages.
  /// @note This is also used to prevent logging.
  self_type &clear();

  friend std::ostream &operator<<(std::ostream &, self_type const &);

  /// Default glue value (a newline) for text rendering.
  static const std::string_view DEFAULT_GLUE;

  /** Test status.

      Equivalent to @c is_ok but more convenient for use in
      control statements.

   *  @return @c false at least one message has a severity of @c FAILURE_SEVERITY or greater, @c
   *  true if not.
   */

  explicit operator bool() const;

  /** Test status.
   *
   * Equivalent to <tt>!is_ok()</tt> but potentially more convenient.
   *
   *  @return @c true at least one message has a severity of @c FAILURE_SEVERITY or greater, @c
   *  false if not.
   */
  bool operator!() const;

  /** Test errata for no failure condition.

      Equivalent to @c operator @c bool but easier to invoke.

      @return @c true if no message has a severity of @c FAILURE_SEVERITY
      or greater, @c false if at least one such message is in the stack.
   */
  bool is_ok() const;

  /** Get the maximum severity of the messages in the erratum.
   *
   * @return Max severity for all messages.
   */
  Severity severity() const;

  /// Number of messages in the errata.
  size_t count() const;
  /// Check for no messages
  /// @return @c true if there is one or messages.
  bool empty() const;

  using iterator       = Container::iterator;
  using const_iterator = Container::const_iterator;

  /// Reference to top item on the stack.
  iterator begin();
  /// Reference to top item on the stack.
  const_iterator begin() const;
  //! Reference one past bottom item on the stack.
  iterator end();
  //! Reference one past bottom item on the stack.
  const_iterator end() const;

  const Annotation &front() const;

  // Logging support.

  /** Base class for erratum sink.

      When an errata is abandoned, this will be called on it to perform any client specific logging.
      It is passed around by handle so that it doesn't have to support copy semantics (and is not
      destructed until application shutdown). Clients can subclass this class in order to preserve
      arbitrary data for the sink or retain a handle to the sink for runtime modifications.
   */
  class Sink {
    using self_type = Sink;

  public:
    using Handle = std::shared_ptr<self_type>; ///< Handle type.

    /// Handle an abandoned errata.
    virtual void operator()(Errata const &) const = 0;
    /// Force virtual destructor.
    virtual ~Sink() {}
  };

  //! Register a sink for discarded erratum.
  static void register_sink(Sink::Handle const &s);

  /// Register a function as a sink.
  using SinkHandler = std::function<void(Errata const &)>;

  /// Convenience wrapper class to enable using functions directly for sinks.
  struct SinkWrapper : public Sink {
    /// Constructor.
    SinkWrapper(SinkHandler f) : _f(f) {}
    /// Operator to invoke the function.
    void operator()(Errata const &e) const override;
    SinkHandler _f; ///< Client supplied handler.
  };

  /// Register a sink function for abandonded erratum.
  static void
  register_sink(SinkHandler const &f) {
    register_sink(Sink::Handle(new SinkWrapper(f)));
  }

  /** Simple formatted output.
   */
  std::ostream &write(std::ostream &out) const;

protected:
  /// Release internal memory.
  void release();

  /// Implementation instance.
  // Although it may seem like move semantics make reference counting unnecessary, that's not the
  // case. The problem is code that wants to work with an instance, which is common. In such cases
  // the instance is constructed just as it is returned (e.g. std::string). Code would therefore
  // have to call std::move for every return, which is not going to be done reliably.
  Data *_data{nullptr};

  /// Force data existence.
  /// @return A pointer to the data.
  const Data *data();

  /// Get a writeable data pointer.
  /// @internal It is a fatal error to ask for writeable data if there are shared references.
  Data *writeable_data();

  /** Allocate a span of memory.
   *
   * @param n Number of bytes to allocate.
   * @return A span of the allocated memory.
   */
  MemSpan<char> alloc(size_t n);

  /// Add a note which is already localized.
  self_type &note_localized(Severity, std::string_view const &text);

  /// Used for returns when no data is present.
  static Annotation const NIL_NOTE;

  friend struct Data;
  friend class Item;
};

extern std::ostream &operator<<(std::ostream &os, Errata const &stat);

/** Return type for returning a value and status (errata).  In general, a method wants to return
    both a result and a status so that errors are logged properly. This structure is used to do that
    in way that is more usable than just @c std::pair.  - Simpler and shorter typography - Force use
    of @c errata rather than having to remember it (and the order) each time - Enable assignment
    directly to @a R for ease of use and compatibility so clients can upgrade asynchronously.
 */
template <typename R> class Rv {
public:
  using result_type = R; ///< Type of result value.

protected:
  using self_type = Rv; ///< Standard self reference type.

  result_type _r; ///< The result.
  Errata _errata; ///< The errata.

public:
  /** Default constructor.
      The default constructor for @a R is used.
      The status is initialized to SUCCESS.
  */
  Rv();

  /** Construct with copy of @a result and empty (successful) Errata.
   *
   * @param result Result of the operation.
   */
  Rv(result_type const &result);

  /** Construct with copy of @a result and move @a errata.
   *
   * @param result Return value / result.
   * @param errata Source errata to move.
   */
  Rv(result_type const &result, Errata &&errata);

  /** Construct with move of @a result and empty (successful) Errata.
   *
   * @param result The return / result value.
   */
  Rv(result_type &&result);

  /** Construct with a move of result and @a errata.
   *
   * @param result The return / result value to move.
   * @param errata Status to move.
   */
  Rv(result_type &&result, Errata &&errata);

  /** Construct only from @a errata
   *
   * @param errata Errata instance.
   *
   * This is useful for error conditions. The result is default constructed and the @a errata
   * consumed by the return value. If @c result_type is a smart pointer or other cheaply default
   * constructed class this can make the code much cleaner;
   *
   * @code
   * // Assume Thing can be default constructed cheaply.
   * Rv<Thing> func(...) {
   *   if (something_bad) {
   *     return Errata().error("Bad thing happen!");
   *   }
   *   return new Thing{arg1, arg2};
   * }
   * @endcode
   */
  Rv(Errata &&errata);

  /** Push a message in to the result.
   *
   * @param level Severity of the message.
   * @param text Text of the message.
   * @return @a *this
   */
  self_type &note(Severity level, std::string_view text);

  /** Push a message in to the result.
   *
   * @tparam Args Format string argument types.
   * @param level Severity of the message.
   * @param fmt Format string.
   * @param args Arguments for @a fmt.
   * @return @a *this
   */
  template <typename... Args> self_type &note(Severity level, std::string_view fmt, Args &&... args);

  /** User conversion to the result type.

      This makes it easy to use the function normally or to pass the result only to other functions
      without having to extract it by hand.
  */
  operator result_type const &() const;

  /** Assignment from result type.

      @param r Result.

      This allows the result to be assigned to a pre-declared return value structure.  The return
      value is a reference to the internal result so that this operator can be chained in other
      assignments to instances of result type. This is most commonly used when the result is
      computed in to a local variable to be both returned and stored in a member.

      @code
      Rv<int> zret;
      int value;
      // ... complex computations, result in value
      this->m_value = zret = value;
      // ...
      return zret;
      @endcode

      @return A reference to the copy of @a r stored in this object.
  */
  result_type &operator=(result_type const &r);

  /** Move assign a result @r to @a this.
   *
   * @param r Result.
   * @return @a r
   */
  result_type &operator=(result_type &&r);

  /** Set the result.

      This differs from assignment of the function result in that the
      return value is a reference to the @c Rv, not the internal
      result. This makes it useful for assigning a result local
      variable and then returning.

   * @param result Value to move.
   * @return @a this

      @code
      Rv<int> func(...) {
        Rv<int> zret;
        int value;
        // ... complex computation, result in value
        return zret.assign(value);
      }
      @endcode
  */
  self_type &assign(result_type const &result);

  /** Move the @a result to @a this.
   *
   * @param result Value to move.
   * @return @a this,
   */
  self_type &assign(result_type &&result);

  /** Return the result.
      @return A reference to the result value in this object.
  */
  result_type &result();

  /** Return the result.
      @return A reference to the result value in this object.
  */
  result_type const &result() const;

  /** Return the status.
      @return A reference to the @c errata in this object.
  */
  Errata &errata();

  /** Return the status.
      @return A reference to the @c errata in this object.
  */
  Errata const &errata() const;

  /** Get the internal @c Errata.
   *
   * @return Reference to internal @c Errata.
   */
  operator Errata &();

  /** Replace current status with @a status.
   *
   * @param status Errata to move in to this instance.
   * @return *this
   */
  self_type &operator=(Errata &&status);

  /** Check the status of the return value.
   *
   * @return @a true if the value is valid / OK, @c false otherwise.
   */
  inline bool is_ok() const;

  /// Clear the errata.
  self_type &clear();
};

/** Combine a function result and status in to an @c Rv.
    This is useful for clients that want to declare the status object
    and result independently.
 */
template <typename R>
Rv<typename std::remove_reference<R>::type>
MakeRv(R &&r,           ///< The function result
       Errata &&erratum ///< The pre-existing status object
) {
  return Rv<typename std::remove_reference<R>::type>(std::forward<R>(r), std::move(erratum));
}
/* ----------------------------------------------------------------------- */
// Inline methods for Annotation

inline Errata::Annotation::Annotation() {}

inline Errata::Annotation::Annotation(Severity severity, std::string_view text) : _severity(severity), _text(text) {}

inline Errata::Annotation &
Errata::Annotation::clear() {
  _severity = Errata::DEFAULT_SEVERITY;
  _text     = std::string_view{};
  return *this;
}

inline std::string_view
Errata::Annotation::text() const {
  return _text;
}

inline unsigned
Errata::Annotation::level() const {
  return _level;
}

inline Errata::Severity
Errata::Annotation::severity() const {
  return _severity;
}

inline Errata::Annotation &
Errata::Annotation::assign(std::string_view text) {
  _text = text;
  return *this;
}

inline Errata::Annotation &
Errata::Annotation::assign(Severity level) {
  _severity = level;
  return *this;
}
/* ----------------------------------------------------------------------- */
// Inline methods for Errata::Data

inline Errata::Data::Data(MemArena &&arena) {
  _arena = std::move(arena);
}

inline swoc::MemSpan<char>
Errata::Data::remnant() {
  return _arena.remnant().rebind<char>();
}

inline swoc::MemSpan<char>
Errata::Data::alloc(size_t n) {
  return _arena.alloc(n).rebind<char>();
}

inline bool
Errata::Data::empty() const {
  return _notes.empty();
}

/* ----------------------------------------------------------------------- */
// Inline methods for Errata

inline Errata::Errata() {}

inline Errata::Errata(self_type &&that) {
  std::swap(_data, that._data);
}

inline Errata::Errata(self_type const &that) {
  if (nullptr != (_data = that._data))
  {
    ++(_data->_ref_count);
  }
}

inline auto
Errata::operator=(self_type &&that) -> self_type & {
  if (this != &that)
  {
    this->release();
    std::swap(_data, that._data);
  }
  return *this;
}

inline Errata::operator bool() const {
  return this->is_ok();
}

inline bool Errata::operator!() const {
  return !this->is_ok();
}

inline bool
Errata::empty() const {
  return _data == nullptr || _data->_notes.count() == 0;
}

inline size_t
Errata::count() const {
  return _data ? _data->_notes.count() : 0;
}

inline bool
Errata::is_ok() const {
  return 0 == _data || 0 == _data->_notes.count() || _data->_severity < FAILURE_SEVERITY;
}

inline const Errata::Annotation &
Errata::front() const {
  return *(_data->_notes.head());
}

template <typename... Args>
Errata &
Errata::note(Severity severity, std::string_view fmt, Args &&... args) {
  return this->note_v(severity, fmt, std::forward_as_tuple(args...));
}

template <typename... Args>
Errata &
Errata::diag(std::string_view fmt, Args &&... args) & {
  return this->note_v(Severity::DIAG, fmt, std::forward_as_tuple(args...));
}

template <typename... Args>
Errata
Errata::diag(std::string_view fmt, Args &&... args) && {
  return std::move(this->note_v(Severity::DIAG, fmt, std::forward_as_tuple(args...)));
}

template <typename... Args>
Errata &
Errata::info(std::string_view fmt, Args &&... args) & {
  return this->note_v(Severity::INFO, fmt, std::forward_as_tuple(args...));
}

template <typename... Args>
Errata
Errata::info(std::string_view fmt, Args &&... args) && {
  return std::move(this->note_v(Severity::INFO, fmt, std::forward_as_tuple(args...)));
}

template <typename... Args>
Errata &
Errata::warn(std::string_view fmt, Args &&... args) & {
  return this->note_v(Severity::WARN, fmt, std::forward_as_tuple(args...));
}

template <typename... Args>
Errata
Errata::warn(std::string_view fmt, Args &&... args) && {
  return std::move(this->note_v(Severity::WARN, fmt, std::forward_as_tuple(args...)));
}

template <typename... Args>
Errata &
Errata::error(std::string_view fmt, Args &&... args) & {
  return this->note_v(Severity::ERROR, fmt, std::forward_as_tuple(args...));
}

template <typename... Args>
Errata
Errata::error(std::string_view fmt, Args &&... args) && {
  return std::move(this->note_v(Severity::ERROR, fmt, std::forward_as_tuple(args...)));
}

inline Errata &
Errata::note(self_type &&that) {
  this->note(that);
  that.clear();
  return *this;
}

template <typename... Args>
Errata &
Errata::note_v(Severity severity, std::string_view fmt, std::tuple<Args...> const &args) {
  Data *data = this->writeable_data();
  auto span  = data->remnant();
  FixedBufferWriter bw{span};
  if (!bw.print_v(fmt, args).error())
  {
    span = span.prefix(bw.extent());
    data->alloc(bw.extent()); // require the part of the remnant actually used.
  } else
  {
    // Not enough space, get a big enough chunk and do it again.
    span = this->alloc(bw.extent());
    FixedBufferWriter{span}.print_v(fmt, args);
  }
  this->note_localized(severity, span.view());
  return *this;
}

inline void
Errata::SinkWrapper::operator()(Errata const &e) const {
  _f(e);
}
/* ----------------------------------------------------------------------- */
// Inline methods for Rv

template <typename R>
inline bool
Rv<R>::is_ok() const {
  return _errata.is_ok();
}

template <typename R>
inline auto
Rv<R>::clear() -> self_type & {
  errata().clear();
}

template <typename T> Rv<T>::Rv() {}

template <typename T> Rv<T>::Rv(result_type const &r) : _r(r) {}

template <typename T> Rv<T>::Rv(result_type const &r, Errata &&errata) : _r(r), _errata(std::move(errata)) {}

template <typename R> Rv<R>::Rv(R &&r) : _r(std::move(r)) {}

template <typename R> Rv<R>::Rv(R &&r, Errata &&errata) : _r(std::move(r)), _errata(std::move(errata)) {}

template <typename R> Rv<R>::Rv(Errata &&errata) : _errata{std::move(errata)} {}

template <typename T> Rv<T>::operator result_type const &() const {
  return _r;
}

template <typename R>
R const &
Rv<R>::result() const {
  return _r;
}

template <typename R>
R &
Rv<R>::result() {
  return _r;
}

template <typename R>
Errata const &
Rv<R>::errata() const {
  return _errata;
}

template <typename R>
Errata &
Rv<R>::errata() {
  return _errata;
}

template <typename R> Rv<R>::operator Errata &() {
  return _errata;
}

template <typename R>
Rv<R> &
Rv<R>::assign(result_type const &r) {
  _r = r;
  return *this;
}

template <typename R>
Rv<R> &
Rv<R>::assign(R &&r) {
  _r = std::move(r);
  return *this;
}

template <typename R>
auto
Rv<R>::operator=(Errata &&errata) -> self_type & {
  _errata = std::move(errata);
  return *this;
}

template <typename R>
auto
Rv<R>::note(Severity level, std::string_view text) -> self_type & {
  _errata.note(level, text);
  return *this;
}

template <typename R>
template <typename... Args>
Rv<R> &
Rv<R>::note(Severity level, std::string_view fmt, Args &&... args) {
  _errata.note_v(level, fmt, std::forward_as_tuple(args...));
  return *this;
}

template <typename R>
auto
Rv<R>::operator=(result_type const &r) -> result_type & {
  _r = r;
  return _r;
}

template <typename R>
auto
Rv<R>::operator=(result_type &&r) -> result_type & {
  _r = std::move(r);
  return _r;
}

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, Severity);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, Errata::Annotation const &);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, Errata const &);

}} // namespace swoc

// Tuple / structured binding support.
namespace std
{
template <size_t IDX, typename R> class tuple_element<IDX, swoc::Rv<R>> { static_assert("swoc:Rv tuple index out of range"); };

template <typename R> class tuple_element<0, swoc::Rv<R>> {
public:
  using type = typename swoc::Rv<R>::result_type;
};

template <typename R> class tuple_element<1, swoc::Rv<R>> {
public:
  using type = swoc::Errata;
};

template <typename R> class tuple_size<swoc::Rv<R>> : public std::integral_constant<size_t, 2> {};

} // namespace std

namespace swoc { inline namespace SWOC_VERSION_NS {
// Not sure how much of this is needed, but experimentally all of these were needed in one
// use case or another of structured binding. I wasn't able to make this work if this was
// defined in namespace @c std. Also, because functions can't be partially specialized, it is
// necessary to use @c constexpr @c if to handle the case. This should roll up nicely when
// compiled.

template<size_t IDX, typename R>
typename std::tuple_element<IDX, swoc::Rv<R>>::type&
get(swoc::Rv<R>&& rv) {
  if constexpr (IDX == 0) {
    return rv.result();
  } else if constexpr (IDX == 1) {
    return rv.errata();
  }
}

template<size_t IDX, typename R>
typename std::tuple_element<IDX, swoc::Rv<R>>::type&
get(swoc::Rv<R>& rv) {
  static_assert(0 <= IDX && IDX <= 1, "Errata tuple index out of range (0..1)");
  if constexpr (IDX == 0) {
    return rv.result();
  } else if constexpr (IDX == 1) {
    return rv.errata();
  }
  // Shouldn't need this due to the @c static_assert but the Intel compiler requires it.
  throw std::domain_error("Errata index value out of bounds");
}

template<size_t IDX, typename R>
typename std::tuple_element<IDX, swoc::Rv<R>>::type const&
get(swoc::Rv<R> const& rv) {
  static_assert(0 <= IDX && IDX <= 1, "Errata tuple index out of range (0..1)");
  if constexpr (IDX == 0) {
    return rv.result();
  } else if constexpr (IDX == 1) {
    return rv.errata();
  }
  // Shouldn't need this due to the @c static_assert but the Intel compiler requires it.
  throw std::domain_error("Errata index value out of bounds");
}

}} // namespace swoc

namespace std {
  using swoc::get; // Import specialized overloads to standard namespace.
}
