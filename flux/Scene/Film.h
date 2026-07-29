#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "flux/Core/Math.h"
#include "kira/Compiler.h"

namespace flux {
namespace detail {
/// \brief Holds a compile-time sequence of types.
template <typename...> struct TypeList {};

/// \brief Delays an invalid-channel diagnostic until template instantiation.
template <typename> inline constexpr bool AlwaysFalse = false;
} // namespace detail

/// \brief Selects the channels allocated for a film.
enum class FilmChannels : std::uint32_t {
    None = 0,
    Normal = 1U << 0U,
    Albedo = 1U << 1U,
    All = (1U << 0U) | (1U << 1U),
};

/// \brief Combines two film-channel selections.
[[nodiscard]] constexpr FilmChannels operator|(FilmChannels lhs, FilmChannels rhs) noexcept {
    return static_cast<FilmChannels>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs)
    );
}

/// \brief Describes the world-space shading-normal channel.
struct NormalChannel {
    /// Value stored for each pixel.
    using Value = Vec3f;

    /// Runtime bit that enables this channel.
    static constexpr FilmChannels flag = FilmChannels::Normal;
};

/// \brief Describes the primary-surface albedo estimator channel.
struct AlbedoChannel {
    /// Value stored for each pixel.
    using Value = Vec3f;

    /// Runtime bit that enables this channel.
    static constexpr FilmChannels flag = FilmChannels::Albedo;
};

/// \brief Type list of all film channels supported by the renderer.
using FilmChannelTypes = detail::TypeList<NormalChannel, AlbedoChannel>;

/// \brief Stores one channel-specific element for every type in \c Channels.
///
/// \tparam Element Template that maps a channel descriptor to its stored element.
/// \tparam Channels Channel sequence represented by this list.
template <template <typename Channel> typename Element, typename Channels = FilmChannelTypes>
struct FilmChannelListOf;

/// \brief Terminates a channel list.
template <template <typename Channel> typename Element>
struct FilmChannelListOf<Element, detail::TypeList<>> {
    /// \brief Applies no operation to an empty list.
    template <typename Function> KIRA_HOST_DEVICE constexpr void forEach(Function &&) {}

    /// \brief Applies no operation to an empty const list.
    template <typename Function> KIRA_HOST_DEVICE constexpr void forEach(Function &&) const {}

    /// \brief Rejects access to a channel outside \c FilmChannelTypes.
    template <typename Channel> KIRA_HOST_DEVICE constexpr auto &get() noexcept {
        static_assert(detail::AlwaysFalse<Channel>, "Unknown film channel");
    }

    /// \brief Rejects const access to a channel outside \c FilmChannelTypes.
    template <typename Channel> KIRA_HOST_DEVICE constexpr auto const &get() const noexcept {
        static_assert(detail::AlwaysFalse<Channel>, "Unknown film channel");
    }
};

/// \brief Stores the front channel followed by the remaining channels.
template <template <typename Channel> typename Element, typename Front, typename... Tail>
struct FilmChannelListOf<Element, detail::TypeList<Front, Tail...>> {
    /// Element associated with the front channel.
    Element<Front> front{};

    /// Elements associated with the remaining channels.
    FilmChannelListOf<Element, detail::TypeList<Tail...>> tail{};

public:
    /// \brief Applies \p function to every element in channel order.
    template <typename Function> KIRA_HOST_DEVICE constexpr void forEach(Function &&function) {
        std::forward<Function>(function)(front);
        tail.forEach(std::forward<Function>(function));
    }

    /// \brief Applies \p function to every element in a const list.
    template <typename Function>
    KIRA_HOST_DEVICE constexpr void forEach(Function &&function) const {
        std::forward<Function>(function)(front);
        tail.forEach(std::forward<Function>(function));
    }

    /// \brief Returns the element associated with \c Channel.
    template <typename Channel> [[nodiscard]] KIRA_HOST_DEVICE constexpr auto &get() noexcept {
        if constexpr (std::is_same_v<Channel, Front>)
            return front;
        else
            return tail.template get<Channel>();
    }

    /// \brief Returns the const element associated with \c Channel.
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
    /// Channel descriptor represented by this view.
    using ChannelType = Channel;

    /// First channel element, or null when the channel is disabled.
    typename Channel::Value *data{};
};

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
                           .template get<NormalChannel>()),
              FilmChannelView<NormalChannel> const &>);
static_assert(std::is_same_v<
              decltype(std::declval<FilmChannelListOf<FilmChannelView> const &>()
                           .template get<AlbedoChannel>()),
              FilmChannelView<AlbedoChannel> const &>);

/// \brief Describes the channelized target owned by a render product.
///
/// Device storage belongs to the active renderer backend. The host object
/// stores only the target shape until readback support is introduced.
class Film final {
public:
    /// \brief Non-owning view assembled by the active renderer backend.
    struct Impl;

    /// \brief Creates a film with the given nonzero dimensions.
    ///
    /// \throw kira::Anyhow If either dimension is zero.
    Film(std::uint32_t width, std::uint32_t height);

    /// \brief Returns the image width in pixels.
    [[nodiscard]] std::uint32_t getWidth() const noexcept { return width_; }

    /// \brief Returns the image height in pixels.
    [[nodiscard]] std::uint32_t getHeight() const noexcept { return height_; }

    /// \brief Changes the target shape.
    ///
    /// Backend storage is resized lazily on the next render.
    /// \throw kira::Anyhow If either dimension is zero.
    void setResolution(std::uint32_t width, std::uint32_t height);

    /// \brief Returns the channels requested by this film.
    [[nodiscard]] FilmChannels getChannels() const noexcept { return channels_; }

    /// \brief Replaces the set of channels requested by this film.
    ///
    /// Backend storage is updated lazily on the next render.
    /// \throw kira::Anyhow If \p channels contains an unknown bit.
    void setChannels(FilmChannels channels);

    /// \brief Returns whether \p channel is requested.
    [[nodiscard]] bool hasChannel(FilmChannels channel) const noexcept;

private:
    std::uint32_t width_;
    std::uint32_t height_;
    FilmChannels channels_{FilmChannels::All};
};

/// \brief Non-owning view of the channels in a film.
struct Film::Impl {
    /// Storage for the channels present in this render.
    FilmChannelListOf<FilmChannelView> channels{};

    /// Image width shared by all present channels.
    std::uint32_t width{};

    /// Image height shared by all present channels.
    std::uint32_t height{};

public:
    /// \brief Returns whether \c Channel is present in this launch.
    template <typename Channel> [[nodiscard]] KIRA_HOST_DEVICE bool hasChannel() const noexcept {
        return channels.template get<Channel>().data != nullptr;
    }

    /// \brief Atomically adds \p value to \c Channel at \p pixel.
    ///
    /// A missing channel discards the value. Concurrent samples of one pixel
    /// may call this function.
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
/// Device representation of a film.
using Film = ::flux::Film::Impl;
} // namespace optix
} // namespace flux
