#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

#include "flux/Core/EnumFlags.h"
#include "flux/Core/Math.h"
#include "flux/Core/Object.h"
#include "kira/Compiler.h"
#include "kira/SmallVector.h"

namespace flux {
class EmbreeAccumulationBuffer;
class OptixAccumulationBuffer;

namespace detail {
template <typename...> struct TypeList {};

template <typename> inline constexpr bool AlwaysFalse = false;
} // namespace detail

/// \brief Identifies the channels of a film.
enum class FilmChannels : std::uint32_t {
    None = 0,
    Normal = 1U << 0U,
    Albedo = 1U << 1U,
    Color = 1U << 2U,
    Depth = 1U << 3U,
    All = (1U << 0U) | (1U << 1U) | (1U << 2U) | (1U << 3U),
};

template <> inline constexpr bool isEnumFlags<FilmChannels> = true;

/// \brief What a film or an accumulation buffer is built from.
struct FilmSpec {
    Vec2u resolution{0U, 0U};
    FilmChannels channels{FilmChannels::None};

    bool operator==(FilmSpec const &) const = default;
};

namespace detail {
/// \brief Checks the spec that a film or a buffer is built from.
///
/// \throw std::invalid_argument If a dimension of the resolution of \p spec is 0 or its channels
/// have an unknown bit.
void checkFilmSpec(FilmSpec const &spec);
} // namespace detail

/// \brief Describes the world-space shading-normal channel.
struct NormalChannel {
    using Value = Vec3f;
    static constexpr FilmChannels flag = FilmChannels::Normal;
};

/// \brief Describes the primary-surface albedo estimator channel.
struct AlbedoChannel {
    using Value = Vec3f;
    static constexpr FilmChannels flag = FilmChannels::Albedo;
};

/// \brief Describes the camera-space depth channel of primary hits.
///
/// A sample that hits a surface adds its depth along the camera's forward axis to x and 1 to y,
/// weighted as the samples of every channel are. A sample that misses adds nothing. x / y is then
/// the mean depth of the hits, and y is 0 where every sample missed.
struct DepthChannel {
    using Value = Vec2f;
    static constexpr FilmChannels flag = FilmChannels::Depth;
};

/// \brief Describes the accumulated scene-linear color channel.
struct ColorChannel {
    using Value = Spectrum;
    static constexpr FilmChannels flag = FilmChannels::Color;
};

/// \brief Type list of all film channels supported by the renderer.
using FilmChannelTypes = detail::TypeList<ColorChannel, NormalChannel, AlbedoChannel, DepthChannel>;

/// \brief Stores one channel-specific element for every type in \c Channels.
///
/// \tparam Element Template that maps a channel type to its stored element.
/// \tparam Channels Channel sequence represented by this list.
template <template <typename Channel> typename Element, typename Channels = FilmChannelTypes>
struct FilmChannelListOf;

/// \brief Terminates a channel list.
template <template <typename Channel> typename Element>
struct FilmChannelListOf<Element, detail::TypeList<>> {
    constexpr FilmChannelListOf() = default;
    template <typename... Arguments>
    constexpr explicit FilmChannelListOf(std::in_place_t, Arguments const &...) noexcept {}

    template <typename Function> KIRA_HOST_DEVICE constexpr void forEach(Function &&) {}
    template <typename Function> KIRA_HOST_DEVICE constexpr void forEach(Function &&) const {}

    template <typename Channel> KIRA_HOST_DEVICE constexpr auto &get() noexcept {
        static_assert(detail::AlwaysFalse<Channel>, "Unknown film channel");
    }

    template <typename Channel> KIRA_HOST_DEVICE constexpr auto const &get() const noexcept {
        static_assert(detail::AlwaysFalse<Channel>, "Unknown film channel");
    }
};

/// \brief Stores the front channel followed by the remaining channels.
template <template <typename Channel> typename Element, typename Front, typename... Tail>
struct FilmChannelListOf<Element, detail::TypeList<Front, Tail...>> {
    Element<Front> front{};
    FilmChannelListOf<Element, detail::TypeList<Tail...>> tail{};

public:
    constexpr FilmChannelListOf() = default;

    /// \brief Constructs every element from \p arguments, which are copied to each.
    template <typename... Arguments>
    constexpr explicit FilmChannelListOf(std::in_place_t, Arguments const &...arguments)
        : front(arguments...), tail(std::in_place, arguments...) {}

    /// \brief Applies \p function to every element in channel order.
    template <typename Function> KIRA_HOST_DEVICE constexpr void forEach(Function &&function) {
        std::forward<Function>(function)(front);
        tail.forEach(std::forward<Function>(function));
    }

    template <typename Function>
    KIRA_HOST_DEVICE constexpr void forEach(Function &&function) const {
        std::forward<Function>(function)(front);
        tail.forEach(std::forward<Function>(function));
    }

    template <typename Channel> [[nodiscard]] KIRA_HOST_DEVICE constexpr auto &get() noexcept {
        if constexpr (std::is_same_v<Channel, Front>)
            return front;
        else
            return tail.template get<Channel>();
    }

    template <typename Channel>
    [[nodiscard]] KIRA_HOST_DEVICE constexpr auto const &get() const noexcept {
        if constexpr (std::is_same_v<Channel, Front>)
            return front;
        else
            return tail.template get<Channel>();
    }
};

/// \brief Non-owning storage view for one film channel.
template <typename Channel> struct FilmChannelView {
    using ChannelType = Channel;

    /// First channel value. A null pointer marks an absent channel.
    typename Channel::Value *data{};
};

static_assert(
    std::is_same_v<
        decltype(std::declval<FilmChannelListOf<FilmChannelView> &>().template get<ColorChannel>()),
        FilmChannelView<ColorChannel> &>
);
static_assert(std::is_same_v<
              decltype(std::declval<FilmChannelListOf<FilmChannelView> &>()
                           .template get<NormalChannel>()),
              FilmChannelView<NormalChannel> &>);
static_assert(std::is_same_v<
              decltype(std::declval<FilmChannelListOf<FilmChannelView> &>()
                           .template get<AlbedoChannel>()),
              FilmChannelView<AlbedoChannel> &>);
static_assert(std::is_same_v<
              decltype(std::declval<FilmChannelListOf<FilmChannelView> const &>()
                           .template get<ColorChannel>()),
              FilmChannelView<ColorChannel> const &>);
static_assert(std::is_same_v<
              decltype(std::declval<FilmChannelListOf<FilmChannelView> const &>()
                           .template get<NormalChannel>()),
              FilmChannelView<NormalChannel> const &>);
static_assert(std::is_same_v<
              decltype(std::declval<FilmChannelListOf<FilmChannelView> const &>()
                           .template get<AlbedoChannel>()),
              FilmChannelView<AlbedoChannel> const &>);

/// \brief Holds the host pixels of an image.
///
/// The resolution and the channels are fixed when the film is built.
/// The values start at zero.
///
/// A film that was moved from holds no channels.
class Film final : private Noncopyable {
public:
    struct Impl;

    /// \brief Creates a film from \p spec.
    ///
    /// \pre Both dimensions of the resolution of \p spec are nonzero.
    /// \pre The channels of \p spec have no unknown bit.
    explicit Film(FilmSpec const &spec);

    Film(Film &&other) noexcept = default;

    Film &operator=(Film other) noexcept {
        swap(*this, other);
        return *this;
    }

    [[nodiscard]] std::uint32_t getWidth() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t getHeight() const noexcept { return height_; }

    /// \brief Returns the spec that the film was built from.
    ///
    /// A film that was moved from has no channels.
    [[nodiscard]] FilmSpec getSpec() const noexcept {
        return {.resolution = {width_, height_}, .channels = getChannels()};
    }

    /// \brief Returns whether the film holds every bit of \p channel.
    ///
    /// Returns false for \c FilmChannels::None.
    [[nodiscard]] bool hasChannel(FilmChannels channel) const noexcept;

    /// \brief Returns the values of \c Channel for all pixels, row by row.
    ///
    /// Returns an empty span if the film does not hold the channel.
    ///
    /// The span stays valid until the film is moved, assigned, swapped, or destroyed.
    template <typename Channel>
    [[nodiscard]] std::span<typename Channel::Value const> getChannel() const noexcept {
        auto const &storage = hostStorage_.template get<Channel>();
        return {storage.values.data(), storage.values.size()};
    }

    /// \brief Swaps two films.
    friend void swap(Film &lhs, Film &rhs) noexcept {
        using std::swap;
        swap(lhs.width_, rhs.width_);
        swap(lhs.height_, rhs.height_);
        lhs.hostStorage_.forEach([&](auto &storage) {
            using Channel = typename std::remove_reference_t<decltype(storage)>::ChannelType;
            swap(storage.values, rhs.hostStorage_.template get<Channel>().values);
        });
    }

private:
    friend class EmbreeAccumulationBuffer;
    friend class OptixAccumulationBuffer;

    template <typename Channel> struct HostChannelStorage {
        using ChannelType = Channel;
        kira::SmallVector<typename Channel::Value, 0> values;
    };

    /// \brief Returns the channels that the film holds.
    [[nodiscard]] FilmChannels getChannels() const noexcept;

    /// \brief Returns a writable view of the channels that the film holds.
    [[nodiscard]] Impl getImpl() noexcept;

    std::uint32_t width_;
    std::uint32_t height_;
    FilmChannelListOf<HostChannelStorage> hostStorage_;
};

/// \brief Writes film channels during rendering.
struct Film::Impl {
    FilmChannelListOf<FilmChannelView> channels{};
    std::uint32_t width{};
    std::uint32_t height{};

public:
    template <typename Channel> [[nodiscard]] KIRA_HOST_DEVICE bool hasChannel() const noexcept {
        return channels.template get<Channel>().data != nullptr;
    }

    /// \brief Atomically adds \p value to \c Channel at \p pixel.
    ///
    /// An absent channel leaves storage unchanged. Concurrent samples of one
    /// pixel may call this function.
    /// \pre \p pixel is in range.
    template <typename Channel>
    KIRA_HOST_DEVICE inline void
    accumulate(Vec2u const &pixel, typename Channel::Value const &value) const noexcept;

    /// \brief Scales every present channel at one linear pixel index.
    ///
    /// \pre \p index is less than \c width times \c height.
    KIRA_HOST_DEVICE inline void scale(std::size_t index, float factor) const noexcept;
};

static_assert(std::is_standard_layout_v<Film::Impl>);
static_assert(std::is_trivially_copyable_v<Film::Impl>);

namespace optix {
using Film = ::flux::Film::Impl;
} // namespace optix
} // namespace flux
