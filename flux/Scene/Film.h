#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

#include "flux/Core/Math.h"
#include "flux/Core/Object.h"
#include "kira/Compiler.h"
#include "kira/SmallVector.h"

namespace flux {
class EmbreeHandler;
class OptixHandler;

namespace detail {
template <typename...> struct TypeList {};

template <typename> inline constexpr bool AlwaysFalse = false;
} // namespace detail

/// \brief Identifies the channels requested by a film.
enum class FilmChannels : std::uint32_t {
    None = 0,
    Normal = 1U << 0U,
    Albedo = 1U << 1U,
    Color = 1U << 2U,
    All = (1U << 0U) | (1U << 1U) | (1U << 2U),
};

[[nodiscard]] constexpr FilmChannels operator|(FilmChannels lhs, FilmChannels rhs) noexcept {
    return static_cast<FilmChannels>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs)
    );
}

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

/// \brief Describes the accumulated scene-linear color channel.
struct ColorChannel {
    using Value = Spectrum;
    static constexpr FilmChannels flag = FilmChannels::Color;
};

/// \brief Type list of all film channels supported by the renderer.
using FilmChannelTypes = detail::TypeList<ColorChannel, NormalChannel, AlbedoChannel>;

/// \brief Stores one channel-specific element for every type in \c Channels.
///
/// \tparam Element Template that maps a channel type to its stored element.
/// \tparam Channels Channel sequence represented by this list.
template <template <typename Channel> typename Element, typename Channels = FilmChannelTypes>
struct FilmChannelListOf;

/// \brief Terminates a channel list.
template <template <typename Channel> typename Element>
struct FilmChannelListOf<Element, detail::TypeList<>> {
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

/// \brief Stores a render product's channel request and downloaded values.
///
/// The renderer backend owns render storage. Film owns the latest downloaded
/// channel values.
class Film final : private Noncopyable {
public:
    /// \brief Non-owning film view used by renderer backends.
    struct Impl;

    /// \brief Creates a film with the given dimensions.
    ///
    /// Both dimensions must be nonzero.
    Film(std::uint32_t width, std::uint32_t height);

    Film(Film &&other) noexcept = default;

    Film &operator=(Film other) noexcept {
        swap(*this, other);
        return *this;
    }

    [[nodiscard]] std::uint32_t getWidth() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t getHeight() const noexcept { return height_; }

    /// \brief Changes the film resolution.
    ///
    /// A changed resolution clears downloaded values. Renderer backends update
    /// their storage on the next render. Both dimensions must be nonzero.
    void setResolution(std::uint32_t width, std::uint32_t height);

    [[nodiscard]] FilmChannels getChannels() const noexcept { return channels_; }

    /// \brief Changes the requested film channels.
    ///
    /// A changed request clears downloaded values. Renderer backends update
    /// their storage on the next render. \p channels may contain known bits
    /// only.
    void setChannels(FilmChannels channels);

    /// \brief Returns whether every bit in \p channel is requested.
    ///
    /// Returns false for \c FilmChannels::None.
    [[nodiscard]] bool hasChannel(FilmChannels channel) const noexcept;

    /// \brief Returns the most recently downloaded values for \c Channel.
    ///
    /// Returns an empty span before download or when the channel is not
    /// requested. Resolution and channel changes clear all downloaded values.
    /// The span remains valid until this film is changed, downloaded, moved,
    /// swapped, or destroyed.
    template <typename Channel>
    [[nodiscard]] std::span<typename Channel::Value const> getChannel() const noexcept {
        auto const &storage = hostStorage_.template get<Channel>();
        return {storage.values.data(), storage.values.size()};
    }

    /// \brief Swaps the resolution, channel request, and downloaded values.
    friend void swap(Film &lhs, Film &rhs) noexcept {
        using std::swap;
        swap(lhs.width_, rhs.width_);
        swap(lhs.height_, rhs.height_);
        swap(lhs.channels_, rhs.channels_);
        lhs.hostStorage_.forEach([&](auto &storage) {
            using Channel = typename std::remove_reference_t<decltype(storage)>::ChannelType;
            swap(storage.values, rhs.hostStorage_.template get<Channel>().values);
        });
    }

private:
    friend class EmbreeHandler;
    friend class OptixHandler;

    template <typename Channel> struct HostChannelStorage {
        using ChannelType = Channel;
        kira::SmallVector<typename Channel::Value, 0> values;
    };

    /// \brief Allocates requested host channels and returns a writable view.
    ///
    /// \warning After an allocation or backend download exception, destroy,
    /// swap, or move-assign this film.
    [[nodiscard]] Impl prepareDownload();

    void invalidateDownload() noexcept;

    std::uint32_t width_;
    std::uint32_t height_;
    FilmChannels channels_{FilmChannels::All};
    FilmChannelListOf<HostChannelStorage> hostStorage_;
};

/// \brief Non-owning view of the channels in a film.
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
